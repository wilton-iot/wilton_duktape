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
};

} // namespace
}

#endif /* WILTON_DUKTAPE_ENGINE_HPP */

