#include <xrpl/ledger/helpers/PaymentChannelHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <variant>

namespace xrpl {

TER
closeChannel(
    SLE::ref slep,
    ApplyViewContext ctx,
    uint256 const& key,
    AccountID const& txAccount,
    beast::Journal j)
{
    ApplyView& view = ctx.view;
    AccountID const src = (*slep)[sfAccount];
    // Remove PayChan from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!view.dirRemove(keylet::ownerDir(src), page, key, true))
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Could not remove paychan from src owner directory";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Remove PayChan from recipient's owner directory, if present.
    AccountID const dst = (*slep)[sfDestination];
    if (auto const page = (*slep)[~sfDestinationNode])
    {
        if (!view.dirRemove(keylet::ownerDir(dst), *page, key, true))
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Could not remove paychan from dst owner directory";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Transfer amount back to owner, decrement owner count
    auto const sle = view.peek(keylet::account(src));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        (*slep)[sfAmount] >= (*slep)[sfBalance], "xrpl::closeChannel : minimum channel amount");

    auto const reqDelta = (*slep)[sfAmount] - (*slep)[sfBalance];
    auto const& issuer = reqDelta.getIssuer();

    // Only update the balance if there is a positive delta.
    if (reqDelta > beast::kZero)
    {
        if (isXRP(reqDelta))
        {
            (*sle)[sfBalance] = (*sle)[sfBalance] + reqDelta;
        }
        else
        {
            if (!view.rules().enabled(featureTokenPaychan))
                return temDISABLED;

            if (auto const ret = std::visit(
                    [&]<typename T>(T const&) {
                        return escrowUnlockPreclaimHelper<T>(view, src, reqDelta, false);
                    },
                    reqDelta.asset().value());
                !isTesSuccess(ret))
                return ret;

            bool const createAsset = src == txAccount;
            if (auto const ret = std::visit(
                    [&]<typename T>(T const&) {
                        return escrowUnlockApplyHelper<T>(
                            ctx,
                            kParityRate,
                            sle,
                            STAmount{(*sle)[sfBalance]}.xrp(),
                            reqDelta,
                            issuer,
                            src,
                            src,
                            createAsset,
                            j);
                    },
                    reqDelta.asset().value());
                !isTesSuccess(ret))
                return ret;
        }
    }

    // Remove PayChan from issuer's owner directory, if present.
    if (auto const optPage = (*slep)[~sfIssuerNode])
    {
        if (!view.dirRemove(keylet::ownerDir(issuer), *optPage, key, true))
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Could not remove paychan from issuer owner directory";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    decreaseOwnerCountForObject(view, sle, slep, 1, j);
    view.update(sle);

    // Remove PayChan from ledger
    view.erase(slep);
    return tesSUCCESS;
}

uint32_t
saturatingAdd(Rules const& rules, uint32_t const lhs, uint32_t const rhs)
{
    if (rules.enabled(fixCleanup3_2_0))
    {
        static constexpr auto kUint32Max =
            static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
        uint64_t const saturatedResult = std::min(uint64_t{lhs} + rhs, kUint32Max);
        return static_cast<uint32_t>(saturatedResult);
    }

    return lhs + rhs;
}

bool
isChannelExpired(ApplyView const& view, std::optional<uint32_t> timeField)
{
    if (!timeField)
        return false;
    if (view.rules().enabled(fixCleanup3_2_0))
        return after(view.header().parentCloseTime, *timeField);
    return view.header().parentCloseTime.time_since_epoch().count() >= *timeField;
}

}  // namespace xrpl
