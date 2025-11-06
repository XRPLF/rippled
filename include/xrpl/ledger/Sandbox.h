#ifndef XRPL_LEDGER_SANDBOX_H_INCLUDED
#define XRPL_LEDGER_SANDBOX_H_INCLUDED

#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/detail/ApplyViewBase.h>

namespace ripple {

/** Discardable, editable view to a ledger.

    The sandbox inherits the flags of the base.

    @note Presented as ApplyView to clients.
*/
class Sandbox : public detail::ApplyViewBase
{
public:
    Sandbox() = delete;
    Sandbox(Sandbox const&) = delete;
    Sandbox&
    operator=(Sandbox&&) = delete;
    Sandbox&
    operator=(Sandbox const&) = delete;

    Sandbox(Sandbox&&) = default;

    Sandbox(ReadView const* base, ApplyFlags flags) : ApplyViewBase(base, flags)
    {
    }

    Sandbox(ApplyView const* base) : Sandbox(base, base->flags())
    {
    }

    void
    apply(RawView& to)
    {
        items_.apply(to);
    }
};

}  // namespace ripple

#endif
