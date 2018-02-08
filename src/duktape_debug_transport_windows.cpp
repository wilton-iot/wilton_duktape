/*
 * Copyright 2016, https://github.com/svaarala/duktape/blob/v1.6-maintenance/AUTHORS.rst
 * Copyright 2018, myasnikov.mike at gmail.com
 * Copyright 2018, alex at staticlibs.net
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
#include "staticlib/support.hpp"

#include "wilton/support/exception.hpp"
#include "wilton/support/logging.hpp"

namespace wilton {
namespace duktape {

namespace { // anonymous

const std::string log_id = "duktape.transport.socket";

} // namespace

// based on https://github.com/svaarala/duktape/blob/v1.6-maintenance/examples/debug-trans-socket/duk_trans_socket_windows.c
class duktape_debug_transport::impl : public sl::pimpl::object::impl {
    int wsa_inited = 0;
    SOCKET server_sock = INVALID_SOCKET;
    SOCKET client_sock = INVALID_SOCKET;
    uint16_t duk_debug_port;

public:
    impl(uint16_t debug_port) :
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
        auto error = std::string();
        auto port_str = sl::support::to_string(duk_debug_port);

        memset((void *) &wsa_data, 0, sizeof(wsa_data));
        memset((void *) &hints, 0, sizeof(hints));

        rc = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (rc != 0) {
            error.assign("WSAStartup() failed: [" + sl::support::to_string(rc) + "]");
            goto fail;
        }
        wsa_inited = 1;

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;
        
        rc = getaddrinfo("0.0.0.0", port_str.c_str(), &hints, &result);
        if (rc != 0) {
            error.assign("getaddrinfo() failed: [" + sl::support::to_string(rc) + "]");
            goto fail;
        }

        server_sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (server_sock == INVALID_SOCKET) {
            error.assign("socket() failed with error: [" + sl::support::to_string(WSAGetLastError()) + "]");
            goto fail;
        }

        rc = bind(server_sock, result->ai_addr, (int) result->ai_addrlen);
        if (rc == SOCKET_ERROR) {
            error.assign("bind() failed with error: [" + sl::support::to_string(WSAGetLastError()) + "]");
            goto fail;
        }

        rc = listen(server_sock, SOMAXCONN);
        if (rc == SOCKET_ERROR) {
            error.assign("listen() failed with error: [" + sl::support::to_string(WSAGetLastError()) + "]");
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
        auto error = std::string();
        auto thread_id = sl::support::to_string_any(std::this_thread::get_id());

        if (server_sock == INVALID_SOCKET) {
            error.assign("no server socket, skip waiting for connection");
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
            error.assign("accept() failed with error [" + sl::support::to_string(WSAGetLastError()) + "], skip waiting for connection");
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
        auto error = std::string();
        int ret;

        if (client_sock == INVALID_SOCKET) {
            return 0;
        }

        if (length == 0) {
            /* This shouldn't happen. */
            error.assign("read request length == 0, closing connection");
            goto fail;
        }

        if (buffer == NULL) {
            /* This shouldn't happen. */
            error.assign("read request buffer == NULL, closing connection");
            goto fail;
        }

        /* In a production quality implementation there would be a sanity
         * timeout here to recover from "black hole" disconnects.
         */

        ret = recv(client_sock, buffer, (int) length, 0);
        if (ret < 0) {
            error.assign("debug read failed, error [" + sl::support::to_string(ret) + "], closing connection");
            goto fail;
        } else if (ret == 0) {
            error.assign("debug read failed, ret == 0 (EOF), closing connection");
            goto fail;
        } else if (ret > (int) length) {
            error.assign("debug read failed, ret too large ([" + sl::support::to_string(ret) + "] > [" + sl::support::to_string(length)+ "]), closing connection");
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
        auto error = std::string();
        int ret;

        if (client_sock == INVALID_SOCKET) {
            return 0;
        }

        if (length == 0) {
            /* This shouldn't happen. */
            error.assign("write request length == 0, closing connection");
            goto fail;
        }

        if (buffer == NULL) {
            /* This shouldn't happen. */
            error.assign("write request buffer == NULL, closing connection");
            goto fail;
        }

        /* In a production quality implementation there would be a sanity
         * timeout here to recover from "black hole" disconnects.
         */

        ret = send(client_sock, buffer, (int) length, 0);
        if (ret <= 0 || ret > (int) length) {
            error.assign("debug write failed, ret: [" + sl::support::to_string(ret) + "], closing connection");
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
        auto error = std::string();
        int rc;

        if (client_sock == INVALID_SOCKET) {
            return 0;
        }

        avail = 0;
        rc = ioctlsocket(client_sock, FIONREAD, &avail);
        if (rc != 0) {
            error.assign("ioctlsocket() returned [" + sl::support::to_string(rc) + "], closing connection");
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
        wilton::support::log_error(log_id, error);
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
