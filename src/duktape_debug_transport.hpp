#ifndef WILTON_DUKTAPE_DEBUG_TRANSPORT_H
#define WILTON_DUKTAPE_DEBUG_TRANSPORT_H

#include "duktape.h"

#include "staticlib/pimpl.hpp"

namespace wilton {
namespace duktape {

class duktape_debug_transport : public sl::pimpl::object {
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
    PIMPL_CONSTRUCTOR(duktape_debug_transport)

    duktape_debug_transport(uint16_t debug_port);

    bool is_active() const;

    uint16_t get_port() const;

    void duk_trans_socket_init();

    void duk_trans_socket_waitconn();

    duk_size_t duk_trans_socket_read_cb(char* buffer, duk_size_t length);

    duk_size_t duk_trans_socket_write_cb(const char* buffer, duk_size_t length);

    duk_size_t duk_trans_socket_peek_cb();
};

} // namespace
}
#endif  /* WILTON_DUKTAPE_DEBUG_TRANSPORT_H */
