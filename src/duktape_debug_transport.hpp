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

#ifndef WILTON_DUKTAPE_DEBUG_TRANSPORT_H
#define WILTON_DUKTAPE_DEBUG_TRANSPORT_H

#include "duktape.h"

#include "staticlib/pimpl.hpp"

namespace wilton {
namespace duktape {

// based on https://github.com/svaarala/duktape/blob/v1.6-maintenance/examples/debug-trans-socket/duk_trans_socket.h
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
