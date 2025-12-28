#pragma once

#include <nes/event_dispatcher.hpp>
#include <windows.h>
#include <spdlog/spdlog.h>
#include <memory>

class SafeEventDispatcher {
public:
    SafeEventDispatcher() = default;

    template<typename event_t>
    void trigger(nes::event_holder<event_t>& e) const {
        __try {
            m_dispatcher.trigger<event_t>(e);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            spdlog::error("Caught an exception in a module! Exception code: {:#x}", GetExceptionCode());
        }
    }

    template<typename event_t, auto handler, auto priority = nes::event_priority_traits<nes::event_priority>::default_value,
             typename class_t = nes::detail::extract_type<decltype(handler)>::class_t>
    void listen(class_t* instance) const {
        m_dispatcher.listen<event_t, handler, priority>(instance);
    }

    template<typename event_t, auto priority = nes::event_priority_traits<nes::event_priority>::default_value>
    void listen(auto handler) const {
        m_dispatcher.listen<event_t, priority>(handler);
    }

    template<typename event_t, auto handler, typename class_t = nes::detail::extract_type<decltype(handler)>::class_t>
    void deafen(class_t* instance) const {
        m_dispatcher.deafen<event_t, handler>(instance);
    }

    template<typename event_t>
    void deafen(auto handler) const {
        m_dispatcher.deafen<event_t>(handler);
    }

private:
    nes::event_dispatcher m_dispatcher;
};