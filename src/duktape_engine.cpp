/* 
 * File:   duktape_engine.cpp
 * Author: alex
 * 
 * Created on May 20, 2017, 2:09 PM
 */

#include "duktape_engine.hpp"

#include <cstring>
#include <functional>
#include <memory>

#include "duktape.h"
#include "duk_trans_socket.h"

#include "staticlib/io.hpp"
#include "staticlib/json.hpp"
#include "staticlib/pimpl/forward_macros.hpp"
#include "staticlib/support.hpp"
#include "staticlib/utils.hpp"

#include "wilton/wiltoncall.h"
#include "wilton/wilton_loader.h"

#include "wilton/support/exception.hpp"
#include "wilton/support/logging.hpp"

namespace wilton {
namespace duktape {

namespace { // anonymous

void fatal_handler(duk_context* , duk_errcode_t code, const char* msg) {
// void fatal_handler(void* recv_code, const char* msg) {
    // duk_errcode_t code = *((duk_errcode_t*) recv_code);
    throw support::exception(TRACEMSG("Duktape fatal error,"
            " code: [" + sl::support::to_string(code) + "], message: [" + msg + "]"));
}

void ctx_deleter(duk_context* ctx) {
    if (nullptr != ctx) {
        duk_destroy_heap(ctx);
    }
}

void pop_stack(duk_context* ctx) {
    duk_pop_n(ctx, duk_get_top(ctx));
}

std::string format_error(duk_context* ctx) {
    if (duk_is_error(ctx, -1)) {
        /* Accessing .stack might cause an error to be thrown, so wrap this
         * access in a duk_safe_call() if it matters.
         */
        duk_get_prop_string(ctx, -1, "stack");
        auto res = std::string(duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return res;
    } else {
        /* Non-Error value, coerce safely to string. */
        return std::string(duk_safe_to_string(ctx, -1));
    }
}

duk_ret_t load_func(duk_context* ctx) {
    std::string path = "";
    try {        
        size_t path_len;
        const char* path_ptr = duk_get_lstring(ctx, 0, std::addressof(path_len));
        if (nullptr == path_ptr) {
            throw support::exception(TRACEMSG("Invalid arguments specified"));
        }    
        path = std::string(path_ptr, path_len);
        // load code
        char* code = nullptr;
        int code_len = 0;
        auto err_load = wilton_load_script(path.c_str(), static_cast<int>(path.length()),
                std::addressof(code), std::addressof(code_len));
        if (nullptr != err_load) {
            support::throw_wilton_error(err_load, TRACEMSG(err_load));
        }
        if (0 == code_len) {
            throw support::exception(TRACEMSG(
                    "\nInvalid empty source code loaded, path: [" + path + "]").c_str());
        }
        wilton::support::log_debug("wilton.engine.duktape.eval",
                "Evaluating source file, path: [" + path + "] ...");
        // compile source

        auto path_short = support::script_engine_map_detail::shorten_script_path(path);

        fprintf(stderr, "[DBG]: loaded file short path: '%s'\n", path_short.c_str());
        fflush(stderr);

        duk_push_lstring(ctx, code, code_len);
        wilton_free(code);
        duk_push_lstring(ctx, path_short.c_str(), path_short.length());
        auto err = duk_pcompile(ctx, DUK_COMPILE_EVAL);
        if (DUK_EXEC_SUCCESS == err) {
            err = duk_pcall(ctx, 0);
        }

        if (DUK_EXEC_SUCCESS != err) {
            std::string msg = format_error(ctx);
            duk_pop(ctx);
            throw support::exception(TRACEMSG(msg + "\nCall error"));
        } else {
            wilton::support::log_debug("wilton.engine.duktape.eval", "Eval complete");
            duk_pop(ctx);
            duk_push_true(ctx);
        }

        return 1;
    } catch (const std::exception& e) {
        throw support::exception(TRACEMSG(e.what() + 
                "\nError(e) loading script, path: [" + path + "]").c_str());
    } catch (...) {
        throw support::exception(TRACEMSG(
                "Error(...) loading script, path: [" + path + "]").c_str());
    }    
}

duk_ret_t wiltoncall_func(duk_context* ctx) {
    size_t name_len;
    const char* name = duk_get_lstring(ctx, 0, std::addressof(name_len));
    if (nullptr == name) {
        name = "";
        name_len = 0;
    }
    size_t input_len;
    const char* input = duk_get_lstring(ctx, 1, std::addressof(input_len));
    if (nullptr == input) {
        input = "";
        input_len = 0;
    }
    char* out = nullptr;
    int out_len = 0;
    wilton::support::log_debug(std::string("wilton.wiltoncall.") + name,
            "Performing a call, input length: [" + sl::support::to_string(input_len) + "] ...");
    auto err = wiltoncall(name, static_cast<int> (name_len), input, static_cast<int> (input_len),
            std::addressof(out), std::addressof(out_len));
    wilton::support::log_debug(std::string("wilton.wiltoncall.") + name,
            "Call complete, result: [" + (nullptr != err ? std::string(err) : "") + "]");
    if (nullptr == err) {
        if (nullptr != out) {
            duk_push_lstring(ctx, out, out_len);
            wilton_free(out);
        } else {
            duk_push_null(ctx);
        }
        return 1;
    } else {
        auto msg = TRACEMSG(err + "\n'wiltoncall' error for name: [" + name + "]");
        wilton_free(err);
        throw support::exception(msg);
    }
}

void register_c_func(duk_context* ctx, const std::string& name, duk_c_function fun, duk_idx_t argnum) {
    duk_push_global_object(ctx);
    duk_push_c_function(ctx, fun, argnum);
    duk_put_prop_string(ctx, -2, name.c_str());
    duk_pop(ctx);
}

void eval_js(duk_context* ctx, const char* code, size_t code_len) {
    auto err = duk_peval_lstring(ctx, code, code_len);
    if (DUK_EXEC_SUCCESS != err) {
        // cannot happen - c++ exception will be thrown by duktape
        throw support::exception(TRACEMSG(format_error(ctx) +
                "\nDuktape engine eval error"));
    }
}

std::string format_stacktrace(duk_context* ctx) {
    static std::string prefix = "Error: caught invalid c++ std::exception '";
    static std::string postfix = "' (perhaps thrown by user code)";
    static std::string anon = "at [anon]";
    static std::string reqjs = "/require.js:";
    auto msg = format_error(ctx);
    sl::utils::replace_all(msg, prefix, "");
    sl::utils::replace_all(msg, postfix, "");
    
    auto src = sl::io::make_buffered_source(sl::io::string_source(msg));
    auto res = std::string();
    std::string line = "";
    bool first = true;
    while(!(line = src.read_line()).empty()) {
        if (std::string::npos == line.find(anon) ||
                std::string::npos == line.find(reqjs)) {
            if (first) {
                first = false;
            } else {
                res.append("\n");
            }
            res.append(line);
        }
    }
    return res;
}

} // namespace

std::string get_cb_module_id(sl::io::span<const char> callback_script_json) {
    auto json = sl::json::load(callback_script_json);
    return json["module"].as_string_nonempty_or_throw();
}

std::string resolve_js_module_path(duk_context* ctx, const std::string& module_id) {
    auto def = sl::support::defer([ctx]() STATICLIB_NOEXCEPT {
        pop_stack(ctx);
    });
    auto callback_script_json = sl::json::dumps({
        {"module", "wilton/loader"},
        {"func", "findModulePath"},
        {"args", [&module_id] {
            auto vec = std::vector<sl::json::value>();
            vec.emplace_back(module_id);
            return vec;
        }()}
    });
    duk_get_global_string(ctx, "WILTON_run");
    duk_push_lstring(ctx, callback_script_json.data(), callback_script_json.size());
    auto err = duk_pcall(ctx, 1);
    if (DUK_EXEC_SUCCESS != err) {
        throw support::exception(TRACEMSG(format_stacktrace(ctx)));
    }
    if (DUK_TYPE_STRING == duk_get_type(ctx, -1)) {
        size_t len;
        const char* str = duk_get_lstring(ctx, -1, std::addressof(len));
        if (len > 0) {
            return std::string(str, static_cast<int> (len));
        }
    }
    return std::string();
}

class duktape_engine::impl : public sl::pimpl::object::impl {
    std::unique_ptr<duk_context, std::function<void(duk_context*)>> dukctx;
    
public:
    impl(sl::io::span<const char> init_code) {
        wilton::support::log_info("wilton.engine.duktape.init", "Initializing engine instance ...");
        this->dukctx = std::unique_ptr<duk_context, std::function<void(duk_context*)>>(
                duk_create_heap(nullptr, nullptr, nullptr, nullptr, fatal_handler), ctx_deleter);
        auto ctx = dukctx.get();
        if (nullptr == ctx) throw support::exception(TRACEMSG(
                "Error creating Duktape context"));
        auto def = sl::support::defer([ctx]() STATICLIB_NOEXCEPT {
            pop_stack(ctx);
        });
        register_c_func(ctx, "WILTON_load", load_func, 1);
        register_c_func(ctx, "WILTON_wiltoncall", wiltoncall_func, 2);
        eval_js(ctx, init_code.data(), init_code.size());
        wilton::support::log_info("wilton.engine.duktape.init", "Engine initialization complete");

        char* config = nullptr;
        int config_len = 0;

        if (nullptr == wilton_config(std::addressof(config), std::addressof(config_len))) {
            fprintf(stderr, "[DBG]: config loaded. length: %d:\n", config_len);
            fprintf(stderr, "'%s':\n", config);
        } else {
            fprintf(stderr, "[DBG]: config NOT loaded\n");
        }

        // get debug connection port
        auto cf = sl::json::load({(const char*) config, config_len});
        auto debug_connection_port = cf["debugConnectionPort"].as_string();

        // if debug port specified - run debugging
        if (!std::string(debug_connection_port).empty()) {
            fprintf(stderr, "[DBG]: debug_connection_port '%s'\n", debug_connection_port.c_str());
            fflush(stderr);

            duk_trans_socket_init(strtoul(debug_connection_port.c_str(), NULL, 0));
            duk_trans_socket_waitconn();
            duk_debugger_attach(ctx,
                                duk_trans_socket_read_cb,
                                duk_trans_socket_write_cb,
                                duk_trans_socket_peek_cb,
                                duk_trans_socket_read_flush_cb,
                                duk_trans_socket_write_flush_cb,
                                NULL,  /* app request cb */
                                NULL);

        }

    }

    support::buffer run_callback_script(duktape_engine&, sl::io::span<const char> callback_script_json) {
        auto ctx = dukctx.get();
        auto def = sl::support::defer([ctx]() STATICLIB_NOEXCEPT {
            pop_stack(ctx);
        });

        
        wilton::support::log_debug("wilton.engine.duktape.run", 
                "Running callback script: [" + std::string(callback_script_json.data(), callback_script_json.size()) + "] ...");
        duk_get_global_string(ctx, "WILTON_run");
        
        std::string module_id = get_cb_module_id(callback_script_json);

        fprintf(stderr, "[DBG]: callback for WILTON_run\n");
        fprintf(stderr, "[DBG]: module_id: '%s.js'\n", module_id.c_str());
        fflush(stderr);

        module_id.append(".js");
        duk_push_lstring(ctx, callback_script_json.data(), callback_script_json.size());
        auto err = duk_pcall(ctx, 1);
        wilton::support::log_debug("wilton.engine.duktape.run",
                "Callback run complete, result: [" + sl::support::to_string_bool(DUK_EXEC_SUCCESS == err) + "]");
        if (DUK_EXEC_SUCCESS != err) {
            throw support::exception(TRACEMSG(format_stacktrace(ctx)));
        }
        if (DUK_TYPE_STRING == duk_get_type(ctx, -1)) {
            size_t len;
            const char* str = duk_get_lstring(ctx, -1, std::addressof(len));
            if (len > 0) {
                return support::make_array_buffer(str, static_cast<int> (len));
            }
        }
        return support::make_empty_buffer();
    }    
};

PIMPL_FORWARD_CONSTRUCTOR(duktape_engine, (sl::io::span<const char>), (), support::exception)
PIMPL_FORWARD_METHOD(duktape_engine, support::buffer, run_callback_script, (sl::io::span<const char>), (), support::exception)


} // namespace
}
