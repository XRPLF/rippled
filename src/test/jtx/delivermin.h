#pragma once

#include <test/jtx/Env.h>

#include <xrpl/protocol/STAmount.h>

namespace xrpl {
namespace test {
namespace jtx {

/** Sets the DeliverMin on a JTx. */
class deliver_min
{
private:
    STAmount amount_;

public:
    deliver_min(STAmount const& amount) : amount_(amount)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
