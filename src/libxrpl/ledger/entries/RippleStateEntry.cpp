#include <xrpl/ledger/entries/RippleStateEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
TER
RippleStateEntry<ViewT>::removeFromLedger(AccountID const& lowAccount, AccountID const& highAccount)
    requires Base::kIsWritable
{
    auto& view = this->applyView();
    auto const j = this->journal();

    // Detect legacy dirs.
    std::uint64_t const uLowNode = (*this)->getFieldU64(sfLowNode);
    std::uint64_t const uHighNode = (*this)->getFieldU64(sfHighNode);

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: low";

    if (!view.dirRemove(keylet::ownerDir(lowAccount), uLowNode, (*this)->key(), false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: high";

    if (!view.dirRemove(keylet::ownerDir(highAccount), uHighNode, (*this)->key(), false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    removeSponsorFromLedgerEntry(this->mutableRawSle(), sfHighSponsor);
    removeSponsorFromLedgerEntry(this->mutableRawSle(), sfLowSponsor);

    JLOG(j.trace()) << "trustDelete: Deleting ripple line: state";
    this->erase();

    return tesSUCCESS;
}

template <typename ViewT>
TER
RippleStateEntry<ViewT>::create(
    ApplyView& view,
    bool const bSrcHigh,
    AccountID const& uSrcAccountID,
    AccountID const& uDstAccountID,
    UInt256 const& uIndex,      // ripple state entry
    SLE::Ref sleAccount,        // the account being set.
    bool const bAuth,           // authorize account.
    bool const bNoRipple,       // others cannot ripple through
    bool const bFreeze,         // funds cannot leave
    bool bDeepFreeze,           // can neither receive nor send funds
    STAmount const& saBalance,  // balance of account being set.
                                // Issuer should be noAccount()
    STAmount const& saLimit,    // limit for account being set.
                                // Issuer should be the account being set.
    std::uint32_t uQualityIn,
    std::uint32_t uQualityOut,
    SLE::Ref sponsorSle,
    beast::Journal j)
    requires Base::kIsWritable
{
    JLOG(j.trace()) << "trustCreate: " << to_string(uSrcAccountID) << ", "
                    << to_string(uDstAccountID) << ", " << saBalance.getFullText();

    auto const& uLowAccountID = !bSrcHigh ? uSrcAccountID : uDstAccountID;
    auto const& uHighAccountID = bSrcHigh ? uSrcAccountID : uDstAccountID;
    if (uLowAccountID == uHighAccountID)
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::trustCreate : trust line to self");
        if (view.rules().enabled(featureLendingProtocol))
            return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    RippleStateEntry sleRippleState(Keylet(ltRIPPLE_STATE, uIndex), view, j);
    sleRippleState.newSLE();
    sleRippleState.insert();

    auto lowNode = view.dirInsert(
        keylet::ownerDir(uLowAccountID), sleRippleState->key(), describeOwnerDir(uLowAccountID));

    if (!lowNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto highNode = view.dirInsert(
        keylet::ownerDir(uHighAccountID), sleRippleState->key(), describeOwnerDir(uHighAccountID));

    if (!highNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    bool const bSetDst = saLimit.getIssuer() == uDstAccountID;
    bool const bSetHigh = bSrcHigh ^ bSetDst;

    XRPL_ASSERT(sleAccount, "xrpl::trustCreate : non-null SLE");
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        sleAccount->getAccountID(sfAccount) == (bSetHigh ? uHighAccountID : uLowAccountID),
        "xrpl::trustCreate : matching account ID");
    auto const slePeer = view.peek(keylet::account(bSetHigh ? uLowAccountID : uHighAccountID));
    if (!slePeer)
        return tecNO_TARGET;

    // Remember deletion hints.
    sleRippleState->setFieldU64(sfLowNode, *lowNode);
    sleRippleState->setFieldU64(sfHighNode, *highNode);

    sleRippleState->setFieldAmount(bSetHigh ? sfHighLimit : sfLowLimit, saLimit);
    sleRippleState->setFieldAmount(
        bSetHigh ? sfLowLimit : sfHighLimit,
        STAmount(Issue{saBalance.get<Issue>().currency, bSetDst ? uSrcAccountID : uDstAccountID}));

    if (uQualityIn != 0u)
        sleRippleState->setFieldU32(bSetHigh ? sfHighQualityIn : sfLowQualityIn, uQualityIn);

    if (uQualityOut != 0u)
        sleRippleState->setFieldU32(bSetHigh ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

    std::uint32_t uFlags = bSetHigh ? lsfHighReserve : lsfLowReserve;

    if (bAuth)
    {
        uFlags |= (bSetHigh ? lsfHighAuth : lsfLowAuth);
    }
    if (bNoRipple)
    {
        uFlags |= (bSetHigh ? lsfHighNoRipple : lsfLowNoRipple);
    }
    if (bFreeze)
    {
        uFlags |= (bSetHigh ? lsfHighFreeze : lsfLowFreeze);
    }
    if (bDeepFreeze)
    {
        uFlags |= (bSetHigh ? lsfHighDeepFreeze : lsfLowDeepFreeze);
    }

    if (!slePeer->isFlag(lsfDefaultRipple))
    {
        // The other side's default is no rippling
        uFlags |= (bSetHigh ? lsfLowNoRipple : lsfHighNoRipple);
    }

    sleRippleState->setFieldU32(sfFlags, uFlags);
    increaseOwnerCount(view, sleAccount, sponsorSle, 1, j);

    addSponsorToLedgerEntry(
        sleRippleState.mutableRawSle(), sponsorSle, bSetHigh ? sfHighSponsor : sfLowSponsor);

    // ONLY: Create ripple balance.
    sleRippleState->setFieldAmount(sfBalance, bSetHigh ? -saBalance : saBalance);

    view.creditHookIOU(uSrcAccountID, uDstAccountID, saBalance, saBalance.zeroed());

    return tesSUCCESS;
}

template class RippleStateEntry<ReadView>;
template class RippleStateEntry<ApplyView>;

}  // namespace xrpl
