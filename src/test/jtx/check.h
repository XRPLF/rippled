#ifndef XRPL_TEST_JTX_CHECK_H_INCLUDED
#define XRPL_TEST_JTX_CHECK_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

namespace ripple {
namespace test {
namespace jtx {

/** Check operations. */
namespace check {

/** Cash a check requiring that a specific amount be delivered. */
Json::Value
cash(jtx::Account const& dest, uint256 const& checkId, STAmount const& amount);

/** Type used to specify DeliverMin for cashing a check. */
struct DeliverMin
{
    STAmount value;
    explicit DeliverMin(STAmount const& deliverMin) : value(deliverMin)
    {
    }
};

/** Cash a check requiring that at least a minimum amount be delivered. */
Json::Value
cash(
    jtx::Account const& dest,
    uint256 const& checkId,
    DeliverMin const& atLeast);

/** Cancel a check. */
Json::Value
cancel(jtx::Account const& dest, uint256 const& checkId);

}  // namespace check

/** Match the number of checks on the account. */
using checks = owner_count<ltCHECK>;

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
