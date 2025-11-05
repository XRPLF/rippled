#ifndef XRPL_TEST_JTX_SENDMAX_H_INCLUDED
#define XRPL_TEST_JTX_SENDMAX_H_INCLUDED

#include <test/jtx/Env.h>

#include <xrpl/protocol/STAmount.h>

namespace ripple {
namespace test {
namespace jtx {

/** Sets the SendMax on a JTx. */
class sendmax
{
private:
    STAmount amount_;

public:
    sendmax(STAmount const& amount) : amount_(amount)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
