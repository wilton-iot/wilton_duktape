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
 * File:   wiltoncall_duktape.cpp
 * Author: alex
 *
 * Created on May 20, 2017, 1:17 PM
 */

#include <memory>
#include <string>

#include "staticlib/config.hpp"
#include "staticlib/io.hpp"

#include "wilton/wilton.h"

#include "wilton/support/buffer.hpp"
#include "wilton/support/exception.hpp"
#include "wilton/support/registrar.hpp"
#include "wilton/support/script_engine_map.hpp"

#include "duktape_engine.hpp"

namespace wilton {
namespace duktape {

// initialized from wilton_module_init
std::shared_ptr<support::script_engine_map<duktape_engine>> shared_tlmap() {
    static auto tlmap = std::make_shared<support::script_engine_map<duktape_engine>>();
    return tlmap;
}

support::buffer runscript(sl::io::span<const char> data) {
    auto tlmap = shared_tlmap();
    return tlmap->run_script(data);
}

support::buffer rungc(sl::io::span<const char>) {
    auto tlmap = shared_tlmap();
    tlmap->run_garbage_collector();
    return support::make_null_buffer();
}

void clean_tls(void*, const char* thread_id, int thread_id_len) {
    auto tlmap = shared_tlmap();
    tlmap->clean_thread_local(thread_id, thread_id_len);
}

} // namespace
}

extern "C" char* wilton_module_init() {
    try {
        wilton::duktape::shared_tlmap();
        auto err = wilton_register_tls_cleaner(nullptr, wilton::duktape::clean_tls);
        if (nullptr != err) wilton::support::throw_wilton_error(err, TRACEMSG(err));
        wilton::support::register_wiltoncall("runscript_duktape", wilton::duktape::runscript);
        wilton::support::register_wiltoncall("rungc_duktape", wilton::duktape::rungc);
        return nullptr;
    } catch (const std::exception& e) {
        return wilton::support::alloc_copy(TRACEMSG(e.what() + "\nException raised"));
    }
}
