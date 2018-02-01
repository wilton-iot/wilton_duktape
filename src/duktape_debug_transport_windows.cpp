/*
 *  Example debug transport using a Windows TCP socket
 *
 *  Provides a TCP server socket which a debug client can connect to.
 *  After that data is just passed through.
 *
 *  https://msdn.microsoft.com/en-us/library/windows/desktop/ms737593(v=vs.85).aspx
 *
 *  Compiling 'duk' with debugger support using MSVC (Visual Studio):
 *
 *    > cl /W3 /O2 /Feduk.exe
 *          /DDUK_OPT_DEBUGGER_SUPPORT /DDUK_OPT_INTERRUPT_COUNTER
 *          /DDUK_CMDLINE_DEBUGGER_SUPPORT
 *          /Iexamples\debug-trans-socket /Isrc
 *          examples\cmdline\duk_cmdline.c
 *          examples\debug-trans-socket\duk_trans_socket_windows.c
 *          src\duktape.c
 *
 */
#include "duktape_debug_transport.hpp"

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "duktape.h"

#include "staticlib/pimpl/forward_macros.hpp"
#include "wilton/support/exception.hpp"
#include "wilton/support/logging.hpp"

namespace wilton {
namespace duktape {

namespace { // anonymous

const int disconnected_state = -1;
const std::string log_id = "duktape.transport.socket";

} // namespace

// based on https://github.com/svaarala/duktape/blob/v1.6-maintenance/examples/debug-trans-socket/duk_trans_socket_windows.c
class duktape_debug_transport::impl : public sl::pimpl::object::impl {
    int server_sock;
    int client_sock;
    uint16_t duk_debug_port;

public:
    impl(uint16_t debug_port) :
    server_sock(disconnected_state),
    client_sock(disconnected_state),
    duk_debug_port(debug_port) { }


    bool is_active(const duktape_debug_transport&) const {
        return 0 != duk_debug_port;
    }

    uint16_t get_port(const duktape_debug_transport&) const {
        return duk_debug_port;
    }

    // For pimpl construction member functions get 'duktape_debug_transport&' parameter
    // By analog in wilton_usb
    void duk_trans_socket_init(duktape_debug_transport&) {
        WSADATA wsa_data;
        struct addrinfo hints;
        struct addrinfo *result = NULL;
        int rc;
        auto error = std::string("");

        memset((void *) &wsa_data, 0, sizeof(wsa_data));
        memset((void *) &hints, 0, sizeof(hints));

        rc = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (rc != 0) {
            fprintf(stderr, "%s: WSAStartup() failed: %d\n", __FILE__, rc);
            fflush(stderr);
            goto fail;
        }
        wsa_inited = 1;

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        rc = getaddrinfo(DUK_DEBUG_ADDRESS, DUK__STRINGIFY(DUK_DEBUG_PORT), &hints, &result);
        if (rc != 0) {
            fprintf(stderr, "%s: getaddrinfo() failed: %d\n", __FILE__, rc);
            fflush(stderr);
            goto fail;
        }

        server_sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (server_sock == INVALID_SOCKET) {
            fprintf(stderr, "%s: socket() failed with error: %ld\n",
                    __FILE__, (long) WSAGetLastError());
            fflush(stderr);
            goto fail;
        }

        rc = bind(server_sock, result->ai_addr, (int) result->ai_addrlen);
        if (rc == SOCKET_ERROR) {
            fprintf(stderr, "%s: bind() failed with error: %ld\n",
                    __FILE__, (long) WSAGetLastError());
            fflush(stderr);
            goto fail;
        }

        rc = listen(server_sock, SOMAXCONN);
        if (rc == SOCKET_ERROR) {
            fprintf(stderr, "%s: listen() failed with error: %ld\n",
                    __FILE__, (long) WSAGetLastError());
            fflush(stderr);
            goto fail;
        }

        if (result != NULL) {
            freeaddrinfo(result);
            result = NULL;
        }
        return;

     fail:
        if (result != NULL) {
            freeaddrinfo(result);
            result = NULL;
        }
        if (server_sock != INVALID_SOCKET) {
            (void) closesocket(server_sock);
            server_sock = INVALID_SOCKET;
        }
        if (wsa_inited) {
            WSACleanup();
            wsa_inited = 0;
        }
        // handle error
        wilton::support::log_error(log_id, error);
        throw support::exception(TRACEMSG(error));
    }

    void duk_trans_socket_waitconn(duktape_debug_transport&) {
        auto error = std::string("");

        if (server_sock == INVALID_SOCKET) {
            fprintf(stderr, "%s: no server socket, skip waiting for connection\n",
                    __FILE__);
            fflush(stderr);
            return;
        }
        if (client_sock != INVALID_SOCKET) {
            (void) closesocket(client_sock);
            client_sock = INVALID_SOCKET;
        }

        std::cout << "Thread, id: [" + thread_id + "]," <<
                " waiting for debug connection on port: [" << duk_debug_port << "]" << std::endl;

        client_sock = accept(server_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) {
            fprintf(stderr, "%s: accept() failed with error %ld, skip waiting for connection\n",
                    __FILE__, (long) WSAGetLastError());
            fflush(stderr);
            goto fail;
        }

        std::cout << "Thread, id: [" + thread_id + "]," <<
                " debug connection established" << std::endl;

        /* XXX: For now, close the listen socket because we won't accept new
         * connections anyway.  A better implementation would allow multiple
         * debug attaches.
         */

        if (server_sock != INVALID_SOCKET) {
            (void) closesocket(server_sock);
            server_sock = INVALID_SOCKET;
        }
        return;

    fail:
        if (client_sock != INVALID_SOCKET) {
            (void) closesocket(client_sock);
            client_sock = INVALID_SOCKET;
        }
        // handle error
        wilton::support::log_error(log_id, error);
        throw support::exception(TRACEMSG(error));
    }

    /*
     *  Duktape callbacks
     */

    /* Duktape debug transport callback: (possibly partial) read. */
    duk_size_t duk_trans_socket_read_cb(duktape_debug_transport&, char *buffer, duk_size_t length) {
        auto error = std::string("");
        int ret;

        if (client_sock == INVALID_SOCKET) {
            return 0;
        }

        if (length == 0) {
            /* This shouldn't happen. */
            fprintf(stderr, "%s: read request length == 0, closing connection\n",
                    __FILE__);
            fflush(stderr);
            goto fail;
        }

        if (buffer == NULL) {
            /* This shouldn't happen. */
            fprintf(stderr, "%s: read request buffer == NULL, closing connection\n",
                    __FILE__);
            fflush(stderr);
            goto fail;
        }

        /* In a production quality implementation there would be a sanity
         * timeout here to recover from "black hole" disconnects.
         */

        ret = recv(client_sock, (void *) buffer, (int) length, 0);
        if (ret < 0) {
            fprintf(stderr, "%s: debug read failed, error %d, closing connection\n",
                    __FILE__, ret);
            fflush(stderr);
            goto fail;
        } else if (ret == 0) {
            fprintf(stderr, "%s: debug read failed, ret == 0 (EOF), closing connection\n",
                    __FILE__);
            fflush(stderr);
            goto fail;
        } else if (ret > (int) length) {
            fprintf(stderr, "%s: debug read failed, ret too large (%ld > %ld), closing connection\n",
                    __FILE__, (long) ret, (long) length);
            fflush(stderr);
            goto fail;
        }

        return (duk_size_t) ret;

     fail:
        if (client_sock != INVALID_SOCKET) {
            (void) closesocket(client_sock);
            client_sock = INVALID_SOCKET;
        }
        wilton::support::log_error(log_id, error);
        return 0;
    }

    /* Duktape debug transport callback: (possibly partial) write. */
    duk_size_t duk_trans_socket_write_cb(duktape_debug_transport&, const char *buffer, duk_size_t length) {
        auto error = std::string("");
        int ret;

        if (client_sock == INVALID_SOCKET) {
            return 0;
        }

        if (length == 0) {
            /* This shouldn't happen. */
            fprintf(stderr, "%s: write request length == 0, closing connection\n",
                    __FILE__);
            fflush(stderr);
            goto fail;
        }

        if (buffer == NULL) {
            /* This shouldn't happen. */
            fprintf(stderr, "%s: write request buffer == NULL, closing connection\n",
                    __FILE__);
            fflush(stderr);
            goto fail;
        }

        /* In a production quality implementation there would be a sanity
         * timeout here to recover from "black hole" disconnects.
         */

        ret = send(client_sock, (const void *) buffer, (int) length, 0);
        if (ret <= 0 || ret > (int) length) {
            fprintf(stderr, "%s: debug write failed, ret %d, closing connection\n",
                    __FILE__, ret);
            fflush(stderr);
            goto fail;
        }

        return (duk_size_t) ret;

     fail:
        if (client_sock != INVALID_SOCKET) {
            (void) closesocket(INVALID_SOCKET);
            client_sock = INVALID_SOCKET;
        }
        wilton::support::log_error(log_id, error);
        return 0;
    }

    duk_size_t duk_trans_socket_peek_cb(duktape_debug_transport&) {
        u_long avail;
        int rc;

        if (client_sock == INVALID_SOCKET) {
            return 0;
        }

        avail = 0;
        rc = ioctlsocket(client_sock, FIONREAD, &avail);
        if (rc != 0) {
            fprintf(stderr, "%s: ioctlsocket() returned %d, closing connection\n",
                    __FILE__, rc);
            fflush(stderr);
            goto fail;  /* also returns 0, which is correct */
        } else {
            if (avail == 0) {
                return 0;  /* nothing to read */
            } else {
                return 1;  /* something to read */
            }
        }
        /* never here */

     fail:
        if (client_sock != INVALID_SOCKET) {
            (void) closesocket(client_sock);
            client_sock = INVALID_SOCKET;
        }
        return 0;
    }
};

PIMPL_FORWARD_CONSTRUCTOR(duktape_debug_transport, (uint16_t), (), support::exception)
PIMPL_FORWARD_METHOD(duktape_debug_transport, bool, is_active, (), (const), support::exception);
PIMPL_FORWARD_METHOD(duktape_debug_transport, uint16_t, get_port, (), (const), support::exception);
PIMPL_FORWARD_METHOD(duktape_debug_transport, void, duk_trans_socket_init, (), (), support::exception);
PIMPL_FORWARD_METHOD(duktape_debug_transport, void, duk_trans_socket_waitconn, (), (), support::exception);
PIMPL_FORWARD_METHOD(duktape_debug_transport, duk_size_t, duk_trans_socket_read_cb, (char*)(duk_size_t), (), support::exception);
PIMPL_FORWARD_METHOD(duktape_debug_transport, duk_size_t, duk_trans_socket_write_cb, (const char*) (duk_size_t), (), support::exception);
PIMPL_FORWARD_METHOD(duktape_debug_transport, duk_size_t, duk_trans_socket_peek_cb, (), (), support::exception);

} // namespace
}
