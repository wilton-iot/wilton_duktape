/*
 * Copyright 2017, alex at staticlibs.net
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
 * File:   duktape_engine.hpp
 * Author: alex
 *
 * Created on May 20, 2017, 2:09 PM
 */

#ifndef WILTON_DUKTAPE_ENGINE_HPP
#define WILTON_DUKTAPE_ENGINE_HPP

#include <string>

#include "staticlib/json.hpp"
#include "staticlib/pimpl.hpp"

#include "wilton/support/buffer.hpp"
#include "wilton/support/exception.hpp"
#include "wilton/support/script_engine_map.hpp"

namespace wilton {
namespace duktape {

class duktape_engine : public sl::pimpl::object {
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
    PIMPL_CONSTRUCTOR(duktape_engine)

    duktape_engine(sl::io::span<const char> init_code);
    
    support::buffer run_callback_script(sl::io::span<const char> callback_script_json);

    void run_garbage_collector();
};

} // namespace
}

#endif /* WILTON_DUKTAPE_ENGINE_HPP */

