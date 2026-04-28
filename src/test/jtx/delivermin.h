#pragma once

#include <test/jtx/Env.h>

#include <xrpl/protocol/STAmount.h>

#include <utility>

namespace xrpl::test::jtx {

/** Sets the DeliverMin on a JTx. */
class deliver_min
{
private:
    STAmount amount_;

public:
    deliver_min(STAmount amount) : amount_(std::move(amount))
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace xrpl::test::jtx
