#if !defined(DUK_TRANS_SOCKET_H_INCLUDED)
#define DUK_TRANS_SOCKET_H_INCLUDED

#include "duktape.h"

#include "staticlib/pimpl.hpp"

namespace wilton {
namespace duktape {
namespace transport {

#if !defined(DUK_DEBUG_PORT)
#define DUK_DEBUG_PORT 9091
#endif

class transport_protocol_socket : public sl::pimpl::object
{
protected:
    /**
     * implementation class
     */
    class impl;

//    const int disconnected_state = -1;
//    int server_sock;
//    int client_sock;
//    unsigned long duk_debug_port;

public:
    /**
     * PIMPL-specific constructor
     *
     * @param pimpl impl object
     */
    PIMPL_CONSTRUCTOR(transport_protocol_socket)

    transport_protocol_socket();

    void duk_trans_socket_init(unsigned long debug_port);
    void duk_trans_socket_finish(void);
    void duk_trans_socket_waitconn(void);
    duk_size_t duk_trans_socket_read_cb(void *udata, char *buffer, duk_size_t length);
    duk_size_t duk_trans_socket_write_cb(void *udata, const char *buffer, duk_size_t length);
    duk_size_t duk_trans_socket_peek_cb(void *udata);
    void duk_trans_socket_read_flush_cb(void *udata);
    void duk_trans_socket_write_flush_cb(void *udata);
};


}
}
}
#endif  /* DUK_TRANS_SOCKET_H_INCLUDED */
