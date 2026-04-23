#pragma once

#include <test/jtx/Env.h>

#include <xrpl/protocol/STAmount.h>

#include <utility>

namespace xrpl::test::jtx {

/** Sets the SendMax on a JTx. */
class sendmax
{
private:
    STAmount amount_;

public:
    sendmax(STAmount amount) : amount_(std::move(amount))
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace xrpl::test::jtx
