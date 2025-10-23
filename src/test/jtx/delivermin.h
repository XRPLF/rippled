#ifndef XRPL_TEST_JTX_DELIVERMIN_H_INCLUDED
#define XRPL_TEST_JTX_DELIVERMIN_H_INCLUDED

#include <test/jtx/Env.h>

#include <xrpl/protocol/STAmount.h>

namespace ripple {
namespace test {
namespace jtx {

/** Sets the DeliverMin on a JTx. */
class delivermin
{
private:
    STAmount amount_;

public:
    delivermin(STAmount const& amount) : amount_(amount)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
