/** @file
 *  Implements the `amm_info` RPC handler, which returns the current state of an
 *  Automated Market Maker (AMM) pool: reserve balances, trading fee, vote slots,
 *  and auction slot. The AMM may be addressed either by its `amm_account` or by
 *  the `asset`+`asset2` pair that identifies the pool.
 */

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>

#include <date/date.h>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

/** Parse an AMM asset from a JSON value, translating parse errors into an RPC error code.
 *
 *  `assetFromJson` throws `std::runtime_error` on malformed input. This wrapper
 *  catches that exception and returns `Unexpected(RpcIssueMalformed)` instead,
 *  keeping raw exceptions out of the RPC dispatch layer.
 *
 *  @param v  JSON value containing the asset specification (currency/issuer object or
 *      `"XRP"` string).
 *  @param j  Journal used to log the parse error message at debug level.
 *  @return The parsed `Asset` on success, or `Unexpected(RpcIssueMalformed)` if
 *      the input is malformed.
 */
Expected<Asset, ErrorCodeI>
getAsset(json::Value const& v, beast::Journal j)
{
    try
    {
        return assetFromJson(v);
    }
    catch (std::runtime_error const& ex)
    {
        JLOG(j.debug()) << "getAsset " << ex.what();
    }
    return Unexpected(RpcIssueMalformed);
}

/** Format a NetClock time point as an ISO 8601 UTC string.
 *
 *  NetClock counts seconds since the Ripple epoch (2000-01-01 00:00:00 UTC),
 *  while `std::chrono::system_clock` uses the Unix epoch (1970-01-01 00:00:00 UTC).
 *  This function bridges the gap by adding `kEPOCH_OFFSET` (946,684,800 s) before
 *  formatting with the Howard Hinnant `date` library. Used exclusively to present
 *  the auction slot's `sfExpiration` field in a universally readable format.
 *
 *  @param tp  NetClock time point to convert.
 *  @return ISO 8601 string in the form `"YYYY-MM-DDTHH:MM:SS+0000"`.
 */
std::string
toIso8601(NetClock::time_point tp)
{
    using namespace std::chrono;
    return date::format(
        "%Y-%Om-%dT%H:%M:%OS%z",
        date::sys_time<system_clock::duration>(
            system_clock::time_point{tp.time_since_epoch() + kEPOCH_OFFSET}));
}

/** Return information about an AMM pool.
 *
 *  Resolves the AMM via `amm_account` (direct keylet by ID) or `asset`+`asset2`
 *  (derives the AMM via `keylet::amm`). Both paths converge to the same AMM SLE.
 *  When only `amm_account` is supplied, the assets are back-filled from the SLE's
 *  `sfAsset`/`sfAsset2` fields.
 *
 *  Pool balances are always fetched with `FreezeHandling::IgnoreFreeze`: an AMM
 *  cannot suspend operation because an issuer froze a trust line. Freeze status is
 *  reported separately via `asset_frozen`/`asset2_frozen` for non-XRP assets.
 *
 *  @param context  RPC dispatch context carrying request parameters, ledger master,
 *      API version, and journal.
 *  @return JSON object containing an `amm` sub-object with pool balances, LP token
 *      supply (or the requesting account's LP balance when `account` is given),
 *      trading fee, vote slots, and auction slot; plus ledger identification fields.
 */
json::Value
doAMMInfo(RPC::JsonContext& context)
{
    auto const& params(context.params);
    json::Value result;

    std::shared_ptr<ReadView const> ledger;
    result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    /** Validated inputs extracted from the RPC request parameters. */
    struct ValuesFromContextParams
    {
        std::optional<AccountID> accountID;
        Asset asset1;
        Asset asset2;
        std::shared_ptr<SLE const> amm;
    };

    /** Extract and validate AMM lookup parameters from the RPC request.
     *
     *  Callers must supply exactly one of: the `asset`+`asset2` pair, or
     *  `amm_account`. Supplying both or neither is `RpcInvalidParams`.
     *
     *  The validity check is evaluated at two different points depending on API
     *  version to preserve backward-compatible error sequencing:
     *  - For `apiVersion < 3` the check fires before individual fields are parsed,
     *    so a conflicting combination short-circuits immediately.
     *  - For `apiVersion >= 3` the check fires after parsing each field, allowing
     *    field-level errors (e.g., malformed asset) to take priority.
     *
     *  @return Populated `ValuesFromContextParams` on success, or
     *      `Unexpected(error_code)` with the appropriate RPC error.
     */
    auto getValuesFromContextParams = [&]() -> Expected<ValuesFromContextParams, ErrorCodeI> {
        std::optional<AccountID> accountID;
        std::optional<Asset> asset1;
        std::optional<Asset> asset2;
        std::optional<uint256> ammID;

        /** Returns true when the combination of asset/amm_account parameters is
         *  invalid: either only one of `asset`/`asset2` is present, or both
         *  `asset` and `amm_account` are present (or both absent).
         */
        constexpr auto kINVALID = [](json::Value const& params) -> bool {
            return (params.isMember(jss::asset) != params.isMember(jss::asset2)) ||
                (params.isMember(jss::asset) == params.isMember(jss::amm_account));
        };

        // NOTE, identical check for apiVersion >= 3 below; placement differs
        // intentionally — see getValuesFromContextParams docstring above.
        if (context.apiVersion < 3 && kINVALID(params))
            return Unexpected(RpcInvalidParams);

        if (params.isMember(jss::asset))
        {
            if (auto const i = getAsset(params[jss::asset], context.j))
            {
                asset1 = *i;
            }
            else
            {
                return Unexpected(i.error());
            }
        }

        if (params.isMember(jss::asset2))
        {
            if (auto const i = getAsset(params[jss::asset2], context.j))
            {
                asset2 = *i;
            }
            else
            {
                return Unexpected(i.error());
            }
        }

        if (params.isMember(jss::amm_account))
        {
            auto const id = parseBase58<AccountID>((params[jss::amm_account].asString()));
            if (!id)
                return Unexpected(RpcActMalformed);
            auto const sle = ledger->read(keylet::account(*id));
            if (!sle)
                return Unexpected(RpcActMalformed);
            ammID = sle->getFieldH256(sfAMMID);
            if (ammID->isZero())
                return Unexpected(RpcActNotFound);
        }

        if (params.isMember(jss::account))
        {
            accountID = parseBase58<AccountID>(params[jss::account].asString());
            if (!accountID || !ledger->read(keylet::account(*accountID)))
                return Unexpected(RpcActMalformed);
        }

        // NOTE, identical check for apiVersion < 3 above; placement differs
        // intentionally — see getValuesFromContextParams docstring above.
        if (context.apiVersion >= 3 && kINVALID(params))
            return Unexpected(RpcInvalidParams);

        XRPL_ASSERT(
            (asset1.has_value() == asset2.has_value()) && (asset1.has_value() != ammID.has_value()),
            "xrpl::doAMMInfo : asset1 and asset2 do match");

        auto const ammKeylet = [&]() {
            if (asset1 && asset2)
                return keylet::amm(*asset1, *asset2);
            XRPL_ASSERT(ammID, "xrpl::doAMMInfo::ammKeylet : ammID is set");
            return keylet::amm(*ammID);
        }();
        auto const amm = ledger->read(ammKeylet);
        if (!amm)
            return Unexpected(RpcActNotFound);
        if (!asset1 && !asset2)
        {
            asset1 = (*amm)[sfAsset];
            asset2 = (*amm)[sfAsset2];
        }

        return ValuesFromContextParams{
            .accountID = accountID, .asset1 = *asset1, .asset2 = *asset2, .amm = amm};
    };

    auto const r = getValuesFromContextParams();
    if (!r)
    {
        RPC::injectError(r.error(), result);
        return result;
    }

    auto const& [accountID, asset1, asset2, amm] = *r;

    auto const ammAccountID = amm->getAccountID(sfAccount);

    // IgnoreFreeze: an AMM cannot suspend operations due to a frozen trust
    // line; freeze status is reported separately via asset_frozen/asset2_frozen.
    auto const [asset1Balance, asset2Balance] = ammPoolHolds(
        *ledger,
        ammAccountID,
        asset1,
        asset2,
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        context.j);
    auto const lptAMMBalance =
        accountID ? ammLPHolds(*ledger, *amm, *accountID, context.j) : (*amm)[sfLPTokenBalance];

    json::Value ammResult;
    asset1Balance.setJson(ammResult[jss::amount]);
    asset2Balance.setJson(ammResult[jss::amount2]);
    lptAMMBalance.setJson(ammResult[jss::lp_token]);
    ammResult[jss::trading_fee] = (*amm)[sfTradingFee];
    ammResult[jss::account] = to_string(ammAccountID);
    json::Value voteSlots(json::ValueType::Array);
    if (amm->isFieldPresent(sfVoteSlots))
    {
        for (auto const& voteEntry : amm->getFieldArray(sfVoteSlots))
        {
            json::Value vote;
            vote[jss::account] = to_string(voteEntry.getAccountID(sfAccount));
            vote[jss::trading_fee] = voteEntry[sfTradingFee];
            vote[jss::vote_weight] = voteEntry[sfVoteWeight];
            voteSlots.append(std::move(vote));
        }
    }
    if (voteSlots.size() > 0)
        ammResult[jss::vote_slots] = std::move(voteSlots);
    XRPL_ASSERT(
        !ledger->rules().enabled(fixInnerObjTemplate) || amm->isFieldPresent(sfAuctionSlot),
        "xrpl::doAMMInfo : auction slot is set");
    if (amm->isFieldPresent(sfAuctionSlot))
    {
        auto const& auctionSlot = safeDowncast<STObject const&>(amm->peekAtField(sfAuctionSlot));
        if (auctionSlot.isFieldPresent(sfAccount))
        {
            json::Value auction;
            auto const timeSlot = ammAuctionTimeSlot(
                ledger->header().parentCloseTime.time_since_epoch().count(), auctionSlot);
            auction[jss::time_interval] = timeSlot ? *timeSlot : kAUCTION_SLOT_TIME_INTERVALS;
            auctionSlot[sfPrice].setJson(auction[jss::price]);
            auction[jss::discounted_fee] = auctionSlot[sfDiscountedFee];
            auction[jss::account] = to_string(auctionSlot.getAccountID(sfAccount));
            auction[jss::expiration] =
                toIso8601(NetClock::time_point{NetClock::duration{auctionSlot[sfExpiration]}});
            if (auctionSlot.isFieldPresent(sfAuthAccounts))
            {
                json::Value auth;
                for (auto const& acct : auctionSlot.getFieldArray(sfAuthAccounts))
                {
                    json::Value jv;
                    jv[jss::account] = to_string(acct.getAccountID(sfAccount));
                    auth.append(jv);
                }
                auction[jss::auth_accounts] = auth;
            }
            ammResult[jss::auction_slot] = std::move(auction);
        }
    }

    if (!isXRP(asset1Balance))
    {
        ammResult[jss::asset_frozen] = isFrozen(*ledger, ammAccountID, asset1);
    }
    if (!isXRP(asset2Balance))
    {
        ammResult[jss::asset2_frozen] = isFrozen(*ledger, ammAccountID, asset2);
    }

    result[jss::amm] = std::move(ammResult);
    if (!result.isMember(jss::ledger_index) && !result.isMember(jss::ledger_hash))
        result[jss::ledger_current_index] = ledger->header().seq;
    result[jss::validated] = context.ledgerMaster.isValidated(*ledger);

    return result;
}

}  // namespace xrpl
