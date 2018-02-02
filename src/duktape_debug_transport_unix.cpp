/*
 *  Example debug transport using a Linux/Unix TCP socket
 *
 *  Provides a TCP server socket which a debug client can connect to.
 *  After that data is just passed through.
 *
 *  On some UNIX systems poll() may not be available but select() is.
 *  The default is to use poll(), but you can switch to select() by
 *  defining USE_SELECT.  See https://daniel.haxx.se/docs/poll-vs-select.html.
 */
#include "duktape_debug_transport.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include <netinet/in.h>
#include <sys/socket.h>

// not found on macOS
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x0
#endif // MSG_NOSIGNAL

#include "duktape.h"

#include "staticlib/support.hpp"
#include "staticlib/pimpl/forward_macros.hpp"

#include "wilton/support/exception.hpp"
#include "wilton/support/logging.hpp"

namespace wilton {
namespace duktape {

namespace { // anonymous

const int disconnected_state = -1;
const std::string log_id = "duktape.transport.socket";

} // namespace

// based on https://github.com/svaarala/duktape/blob/v1.6-maintenance/examples/debug-trans-socket/duk_trans_socket_unix.c
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
        struct sockaddr_in addr;
        int on;
        auto error = std::string();

        server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            error.assign("failed to create server socket: [" + std::string(strerror(errno)) + "]");
            goto fail;
        }

        on = 1;
        if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on)) < 0) {
            error.assign("failed to set SO_REUSEADDR for server socket: [" + std::string(strerror(errno)) + "]");
            goto fail;
        }

        memset((void *) &addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(duk_debug_port);

        if (bind(server_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
            error.assign("failed to bind server socket: [" + std::string(strerror(errno)) + "]");
            goto fail;
        }

        listen(server_sock, 1 /*backlog*/);
        return;

     fail:
        if (server_sock >= 0) {
            (void) close(server_sock);
            server_sock = -1;
        }
        // handle error
        wilton::support::log_error(log_id, error);
        throw support::exception(TRACEMSG(error));
    }

    void duk_trans_socket_waitconn(duktape_debug_transport&) {
        struct sockaddr_in addr;
        socklen_t sz;
        auto error = std::string();
        auto thread_id = sl::support::to_string_any(std::this_thread::get_id());

        if (server_sock < 0) {
            error.assign("no server socket, skip waiting for connection;");
            goto fail;
        }
        if (client_sock >= 0) {
            (void) close(client_sock);
            client_sock = -1;
        }

        std::cout << "Thread, id: [" + thread_id + "]," <<
                " waiting for debug connection on port: [" << duk_debug_port << "]" << std::endl;

        sz = (socklen_t) sizeof(addr);
        client_sock = accept(server_sock, (struct sockaddr *) &addr, &sz);
        if (client_sock < 0) {
            error.assign("accept() failed, skip waiting for connection: ["+ std::string(strerror(errno)) + "]");
            goto fail;
        }

        std::cout << "Thread, id: [" + thread_id + "]," <<
                " debug connection established" << std::endl;

        /* XXX: For now, close the listen socket because we won't accept new
         * connections anyway.  A better implementation would allow multiple
         * debug attaches.
         */

        if (server_sock >= 0) {
            (void) close(server_sock);
            server_sock = -1;
        }
        return;

     fail:
        if (client_sock >= 0) {
            (void) close(client_sock);
            client_sock = -1;
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

        if (client_sock < 0) {
            return 0;
        }

        ssize_t ret;
        auto error = std::string();

        if (length == 0) {
            /* This shouldn't happen. */
            error.assign("read request length == 0, closing connection;");
            goto fail;
        }

        if (buffer == NULL) {
            /* This shouldn't happen. */
            error.assign("read request buffer == NULL, closing connection;");
            goto fail;
        }

        /* In a production quality implementation there would be a sanity
         * timeout here to recover from "black hole" disconnects.
         */

        ret = read(client_sock, (void *) buffer, (size_t) length);
        if (ret < 0) {
            error.assign("debug read failed, closing connection: [" + std::string(strerror(errno)) + "]");
            goto fail;
        } else if (ret == 0) {
            error.assign("debug read failed, ret == 0 (EOF), closing connection;");
            goto fail;
        } else if (ret > (ssize_t) length) {
            error.assign("debug read failed, ret too large ([" +
                    sl::support::to_string(ret) + "] > [" + sl::support::to_string(ret) + "]), closing connection;");
            goto fail;
        }

        return (duk_size_t) ret;

     fail:
        if (client_sock >= 0) {
            (void) close(client_sock);
            client_sock = -1;
        }
        wilton::support::log_error(log_id, error);
        return 0;
    }

    /* Duktape debug transport callback: (possibly partial) write. */
    duk_size_t duk_trans_socket_write_cb(duktape_debug_transport&, const char *buffer, duk_size_t length) {

        if (client_sock < 0) {
            return 0;
        }

        ssize_t ret;
        auto error = std::string();

        if (length == 0) {
            /* This shouldn't happen. */
            error.assign("write request length == 0, closing connection;");
            goto fail;
        }

        if (buffer == NULL) {
            /* This shouldn't happen. */
            error.assign("write request buffer == NULL, closing connection;");
            goto fail;
        }

        /* In a production quality implementation there would be a sanity
         * timeout here to recover from "black hole" disconnects.
         */
        ret = send(client_sock, (const void *) buffer, (size_t) length, MSG_NOSIGNAL);
        if (ret <= 0 || ret > (ssize_t) length) {
            error.assign("debug write failed, closing connection;");
            goto fail;
        }

        return (duk_size_t) ret;

     fail:
        if (client_sock >= 0) {
            (void) close(client_sock);
            client_sock = -1;
        }
        wilton::support::log_error(log_id, error);
        return 0;
    }

    duk_size_t duk_trans_socket_peek_cb(duktape_debug_transport&) {

        if (client_sock < 0) {
            return 0;
        }

        struct pollfd fds[1];
        int poll_rc;

        fds[0].fd = client_sock;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        poll_rc = poll(fds, 1, 0);
        if (poll_rc < 0) {
            wilton::support::log_error(log_id, "poll returned < 0, closing connection: [" + std::string(strerror(errno)) + "]");
            goto fail;  /* also returns 0, which is correct */
        } else if (poll_rc > 1) {
            wilton::support::log_error(log_id, "poll returned > 1, treating like 1;");
            return 1;  /* should never happen */
        } else if (poll_rc == 0) {
            return 0;  /* nothing to read */
        } else {
            return 1;  /* something to read */
        }

     fail:
        if (client_sock >= 0) {
            (void) close(client_sock);
            client_sock = -1;
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
