#pragma once

#include <xrpl/beast/insight/HookImpl.h>

#include <memory>

namespace beast::insight {

/** A reference to a handler for performing polled collection. */
class Hook final
{
public:
    /** Create a null hook.
        A null hook has no associated handler.
    */
    Hook() = default;

    /** Create a hook referencing the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Hook(std::shared_ptr<HookImpl> const& impl) : m_impl(impl)
    {
    }

    [[nodiscard]] std::shared_ptr<HookImpl> const&
    impl() const
    {
        return m_impl;
    }

private:
    std::shared_ptr<HookImpl> m_impl;
};

}  // namespace beast::insight
