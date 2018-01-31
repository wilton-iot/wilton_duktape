#ifndef WILTON_DUK_TRANS_SOCKET_H
#define WILTON_DUK_TRANS_SOCKET_H

#include "duktape.h"
#include "staticlib/pimpl.hpp"

namespace wilton {
namespace duktape {
namespace transport {

class transport_protocol_socket : public sl::pimpl::object
{
protected:
    /**
     * implementation class
     */
    class impl;

public:
    /**
     * PIMPL-specific constructor
     *
     * @param pimpl impl object
     */
    PIMPL_CONSTRUCTOR(transport_protocol_socket)

    transport_protocol_socket();

    void duk_trans_socket_init(uint16_t debug_port);
    void duk_trans_socket_waitconn();
    duk_size_t duk_trans_socket_read_cb(char *buffer, duk_size_t length);
    duk_size_t duk_trans_socket_write_cb(const char *buffer, duk_size_t length);
    duk_size_t duk_trans_socket_peek_cb();
};


}
}
}
#endif  /* WILTON_DUK_TRANS_SOCKET_H */
