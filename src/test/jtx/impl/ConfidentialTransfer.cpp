#include <test/jtx/ConfidentialTransfer.h>

#include <test/jtx/Account.h>
#include <test/jtx/mpt.h>

#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <vector>

namespace xrpl {

void
ConfidentialTransferTestBase::setupConfidentialIssuance(
    test::jtx::MPTTester& mpt,
    test::jtx::Account const& issuer,
    std::vector<test::jtx::Account> const& holders,
    std::vector<test::jtx::Account> const& keyOwners,
    std::uint32_t flags)
{
    using namespace test::jtx;
    mpt.create({
        .ownerCount = 1,
        .flags = flags,
    });

    for (auto const& holder : holders)
    {
        mpt.authorize({.account = holder});
        mpt.pay(issuer, holder, 100);
        mpt.generateKeyPair(holder);
    }

    mpt.generateKeyPair(issuer);
    for (auto const& keyOwner : keyOwners)
        mpt.generateKeyPair(keyOwner);
}

}  // namespace xrpl
