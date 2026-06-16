#pragma once

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <limits>
#include <memory>

namespace xrpl {

inline bool
preparePathfindingSandboxForSponsoredDestination(
    PaymentSandbox& sandbox,
    AccountID const& srcAccount,
    AccountID const& dstAccount,
    bool sponsorCreatedAccount)
{
    if (!sponsorCreatedAccount || sandbox.exists(keylet::account(dstAccount)))
        return true;

    auto const sponsor = sandbox.peek(keylet::account(srcAccount));
    if (!sponsor)
        return false;

    auto const currentSponsoringAccountCount = sponsor->getFieldU32(sfSponsoringAccountCount);
    if (currentSponsoringAccountCount == std::numeric_limits<std::uint32_t>::max())
        return false;

    sponsor->setFieldU32(sfSponsoringAccountCount, currentSponsoringAccountCount + 1);
    sandbox.update(sponsor);

    auto const k = keylet::account(dstAccount);
    auto sleDst = std::make_shared<SLE>(k);
    sleDst->setAccountID(sfAccount, dstAccount);
    sleDst->setFieldU32(sfSequence, sandbox.seq());
    sleDst->setFieldAmount(sfBalance, XRPAmount(beast::kZero));
    sleDst->setAccountID(sfSponsor, srcAccount);
    sandbox.insert(sleDst);

    return true;
}

inline STAmount
largestAmount(STAmount const& amt)
{
    return amt.asset().visit(
        [&](Issue const& issue) -> STAmount {
            if (issue.native())
                return kInitialXrp;
            return STAmount(amt.asset(), STAmount::kMaxValue, STAmount::kMaxOffset);
        },
        [&](MPTIssue const&) { return STAmount(amt.asset(), kMaxMpTokenAmount, 0); });
}

inline STAmount
convertAmount(STAmount const& amt, bool all)
{
    if (!all)
        return amt;

    return largestAmount(amt);
};

inline bool
convertAllCheck(STAmount const& a)
{
    return a == largestAmount(a);
}

}  // namespace xrpl
