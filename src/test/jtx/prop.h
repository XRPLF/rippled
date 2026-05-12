#pragma once

#include <test/jtx/Env.h>

#include <memory>

namespace xrpl::test::jtx {

/** Set a property on a JTx. */
template <class T>
struct Prop
{
    std::unique_ptr<BasicProp> p;

    template <class... Args>
    Prop(Args&&... args) : p(std::make_unique<PropType<T>>(std::forward<Args>(args)...))
    {
    }

    void
    operator()(Env& env, JTx& jt) const
    {
        jt.set(p->clone());
    }
};

}  // namespace xrpl::test::jtx
