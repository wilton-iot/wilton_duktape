/* 
 * File:   wiltoncall_duktape.cpp
 * Author: alex
 *
 * Created on May 20, 2017, 1:17 PM
 */

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "staticlib/config.hpp"
#include "staticlib/io.hpp"
#include "staticlib/json.hpp"
#include "staticlib/utils.hpp"

#include "wilton/wilton.h"

#include "wilton/support/buffer.hpp"
#include "wilton/support/exception.hpp"
#include "wilton/support/registrar.hpp"

#include "duktape_engine.hpp"

namespace wilton {
namespace duktape {

namespace { // namespace

std::mutex& static_engines_mutex() {
    static std::mutex mutex;
    return mutex;
}

bool& static_running_flag() {
    static bool flag = true;
    return flag;
}

class destruction_watcher {
public:
    ~destruction_watcher() STATICLIB_NOEXCEPT {
        std::lock_guard<std::mutex> guard{static_engines_mutex()};
        bool& flag = static_running_flag();
        flag = false;
    }
};

// cleaned up manually due to lack of portable TLS
std::unordered_map<std::string, std::shared_ptr<wilton::duktape::duktape_engine>>& static_engines() {
    static_running_flag(); // will be destroyed last
    static std::unordered_map<std::string, std::shared_ptr<wilton::duktape::duktape_engine>> engines;
    static destruction_watcher watcher;
    return engines;
}

// no TLS in vs2013
std::shared_ptr<wilton::duktape::duktape_engine> thread_local_engine(
        const std::string& requirejs_dir_path) {
    std::lock_guard<std::mutex> guard{static_engines_mutex()};
    auto& map = static_engines();
    auto tid = sl::support::to_string_any(std::this_thread::get_id());
    auto it = map.find(tid);
    if (map.end() == it) {
        auto se = std::make_shared<wilton::duktape::duktape_engine>(requirejs_dir_path);
        auto pa = map.emplace(tid, std::move(se));
        it = pa.first;
    }
    return it->second;
}

} // anonymous

support::buffer runscript(sl::io::span<const char> data) {
    static const std::string& requirejs_dir_path = [] {
        char* conf = nullptr;
        int conf_len = 0;
        auto err = wilton_config(std::addressof(conf), std::addressof(conf_len));
        if (nullptr != err) support::throw_wilton_error(err, TRACEMSG(err));
        const char* cconf = const_cast<const char*>(conf);
        auto json = sl::json::load({cconf, conf_len});
        wilton_free(conf);
        return json["requireJs"]["baseUrl"].as_string_nonempty_or_throw("requireJs.baseUrl") + "/wilton-requirejs";
    }();
    
    // todo: fixme
    auto json_str = std::string(data.data(), data.size());
    auto en = thread_local_engine(requirejs_dir_path);
    auto res = en->run_script(json_str);
    return !res.empty() ? support::make_string_buffer(res) : support::make_empty_buffer();
}


// race condition with registry destructor is here
// band-aid-like solution with destruction_watched seems to work here
void clean_thread_local(const std::string& tid) STATICLIB_NOEXCEPT {
    std::lock_guard<std::mutex> guard{static_engines_mutex()};
    if (static_running_flag()) {
        auto& map = static_engines();
        map.erase(tid);
    }
}

} // namespace
}

extern "C" char* wilton_module_init() {
    try {
        auto err = wilton_register_tls_cleaner(nullptr, [](void*, const char* thread_id, int thread_id_len) {
            if (nullptr != thread_id && thread_id_len > 0) {
                auto tid_str = std::string(thread_id, thread_id_len);
                wilton::duktape::clean_thread_local(tid_str);
            }
        });
        if (nullptr != err) wilton::support::throw_wilton_error(err, TRACEMSG(err));
        wilton::support::register_wiltoncall("runscript_duktape", wilton::duktape::runscript);
        return nullptr;
    } catch (const std::exception& e) {
        return wilton::support::alloc_copy(TRACEMSG(e.what() + "\nException raised"));
    }
}
