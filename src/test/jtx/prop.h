#pragma once

#include <test/jtx/Env.h>

#include <memory>

namespace xrpl {
namespace test {
namespace jtx {

/** Set a property on a JTx. */
template <class Prop>
struct prop
{
    std::unique_ptr<basic_prop> p_;

    template <class... Args>
    prop(Args&&... args) : p_(std::make_unique<prop_type<Prop>>(std::forward<Args>(args)...))
    {
    }

    void
    operator()(Env& env, JTx& jt) const
    {
        jt.set(p_->clone());
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
