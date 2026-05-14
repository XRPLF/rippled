#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/TrustLine.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace xrpl {

/** Appends one trust line's JSON representation to a response array.
 *
 *  Produces the canonical shape of every element in the `lines` array returned
 *  by the `account_lines` RPC command. The `balance` field follows XRPL's sign
 *  convention: positive means the requesting account holds the peer's IOUs;
 *  negative means the peer holds the requesting account's IOUs.
 *
 *  Boolean flag fields (`authorized`, `peer_authorized`, `no_ripple`,
 *  `no_ripple_peer`, `freeze`, `freeze_peer`, `deep_freeze`, `deep_freeze_peer`)
 *  are emitted only when `true`. Callers may treat the absence of a key as
 *  equivalent to `false`. This compactness choice reduces response size
 *  substantially when most lines are in a neutral state.
 *
 *  @param jsonLines  The JSON array to append to; modified in place.
 *  @param line       A perspective-normalized trust line for the requesting account.
 */
void
addLine(json::Value& jsonLines, RPCTrustLine const& line)
{
    STAmount const& saBalance(line.getBalance());
    STAmount const& saLimit(line.getLimit());
    STAmount const& saLimitPeer(line.getLimitPeer());
    json::Value& jPeer(jsonLines.append(json::ValueType::Object));

    jPeer[jss::account] = to_string(line.getAccountIDPeer());
    jPeer[jss::balance] = saBalance.getText();
    jPeer[jss::currency] = to_string(saBalance.get<Issue>().currency);
    jPeer[jss::limit] = saLimit.getText();
    jPeer[jss::limit_peer] = saLimitPeer.getText();
    jPeer[jss::quality_in] = line.getQualityIn().value;
    jPeer[jss::quality_out] = line.getQualityOut().value;
    if (line.getAuth())
        jPeer[jss::authorized] = true;
    if (line.getAuthPeer())
        jPeer[jss::peer_authorized] = true;
    if (line.getNoRipple())
        jPeer[jss::no_ripple] = true;
    if (line.getNoRipplePeer())
        jPeer[jss::no_ripple_peer] = true;
    if (line.getFreeze())
        jPeer[jss::freeze] = true;
    if (line.getFreezePeer())
        jPeer[jss::freeze_peer] = true;
    if (line.getDeepFreeze())
        jPeer[jss::deep_freeze] = true;
    if (line.getDeepFreezePeer())
        jPeer[jss::deep_freeze_peer] = true;
}

/** Implements the `account_lines` RPC command.
 *
 *  Returns the trust lines (IOU credit relationships) associated with an
 *  account, expressed as a paginated JSON array under the `lines` key. Each
 *  element is produced by `addLine()` and represents one `ltRIPPLE_STATE`
 *  ledger entry from the account's owner directory, normalized to the
 *  requesting account's perspective.
 *
 *  **Input fields:**
 *  - `account` (required, string): The Base58Check-encoded account address.
 *  - `ledger_hash` / `ledger_index` (optional): Ledger version to query.
 *  - `peer` (optional, string): If present, only trust lines with this
 *      counterparty are returned.
 *  - `limit` (optional, integer): Page size, clamped to
 *      `[kACCOUNT_LINES.rmin, kACCOUNT_LINES.rmax]` (10–400); default 200.
 *  - `marker` (optional, string): Opaque pagination cursor returned by a
 *      previous call; format is `"<hex_key>,<uint64_hint>"`.
 *  - `ignore_default` (optional, bool): When `true`, trust lines that are in
 *      the default state on the requesting account's side (i.e., neither
 *      `lsfLowReserve` nor `lsfHighReserve` is set for that account) are
 *      omitted. Such lines consume no owner reserve slot and are invisible to
 *      most balance-sheet analyses.
 *
 *  **Marker security:** Before resuming pagination the handler reads the SLE
 *  at the marker key and calls `RPC::isRelatedToAccount()` to verify that the
 *  object belongs to the requested account. A marker pointing to another
 *  account's entry, or to a since-deleted object, returns `rpcINVALID_PARAMS`.
 *
 *  **Pagination idiom:** `forEachItemAfter` is called with `limit + 1`. If the
 *  callback fires exactly `limit + 1` times a next page exists; the marker is
 *  set at the limit-th item's key and emitted in the response. The double-check
 *  `count == limit + 1 && marker` guards the edge case where the directory ends
 *  precisely on the limit boundary.
 *
 *  Sets `context.loadType = kFEE_MEDIUM_BURDEN_RPC` unconditionally before
 *  returning to signal moderate throttling cost to the resource layer.
 *
 *  @param context  The RPC dispatch context for this call, including params,
 *      ledger access, and resource accounting.
 *  @return A JSON object containing `account`, `lines` array, and optionally
 *      `limit` and `marker` when more pages remain.
 */
json::Value
doAccountLines(RPC::JsonContext& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::account))
        return RPC::missingFieldError(jss::account);

    if (!params[jss::account].isString())
        return RPC::invalidFieldError(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::injectError(RpcActMalformed, result);
        return result;
    }
    auto const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(RpcActNotFound);

    std::string strPeer;
    if (params.isMember(jss::peer))
        strPeer = params[jss::peer].asString();

    auto const raPeerAccount = [&]() -> std::optional<AccountID> {
        return strPeer.empty() ? std::nullopt : parseBase58<AccountID>(strPeer);
    }();
    if (!strPeer.empty() && !raPeerAccount)
    {
        RPC::injectError(RpcActMalformed, result);
        return result;
    }

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kACCOUNT_LINES, context))
        return *err;

    bool const ignoreDefault =
        params.isMember(jss::ignore_default) && params[jss::ignore_default].asBool();

    json::Value& jsonLines(result[jss::lines] = json::ValueType::Array);

    /** Captures per-request state shared across the `forEachItemAfter` callback.
     *
     *  Aggregates collected trust lines and the filter parameters in a single
     *  struct so the lambda can capture one pointer rather than five separate
     *  references.
     */
    struct VisitData
    {
        std::vector<RPCTrustLine> items; /**< Trust lines accumulated so far. */
        AccountID const& accountID;      /**< The requesting account. */
        std::optional<AccountID> const& raPeerAccount; /**< Optional peer filter. */
        bool ignoreDefault;  /**< When true, skip default-state lines. */
        uint32_t foundCount; /**< Count of matching lines (unused; kept for future use). */
    };
    VisitData visitData = {
        .items = {},
        .accountID = accountID,
        .raPeerAccount = raPeerAccount,
        .ignoreDefault = ignoreDefault,
        .foundCount = 0};
    uint256 startAfter = beast::kZERO;
    std::uint64_t startHint = 0;

    if (params.isMember(jss::marker))
    {
        if (!params[jss::marker].isString())
            return RPC::expectedFieldError(jss::marker, "string");

        // Format: "<hex_key>,<uint64_hint>" — key is read as hex,
        // hint via boost::lexical_cast.
        std::stringstream marker(params[jss::marker].asString());
        std::string value;
        if (!std::getline(marker, value, ','))
            return rpcError(RpcInvalidParams);

        if (!startAfter.parseHex(value))
            return rpcError(RpcInvalidParams);

        if (!std::getline(marker, value, ','))
            return rpcError(RpcInvalidParams);

        try
        {
            startHint = boost::lexical_cast<std::uint64_t>(value);
        }
        catch (boost::bad_lexical_cast&)
        {
            return rpcError(RpcInvalidParams);
        }

        // Security: reject markers that point to objects belonging to a
        // different account, preventing cross-account directory traversal.
        auto const sle = ledger->read({ltANY, startAfter});

        if (!sle)
            return rpcError(RpcInvalidParams);

        if (!RPC::isRelatedToAccount(*ledger, sle, accountID))
            return rpcError(RpcInvalidParams);
    }

    auto count = 0;
    std::optional<uint256> marker = {};
    std::uint64_t nextHint = 0;
    {
        if (!forEachItemAfter(
                *ledger,
                accountID,
                startAfter,
                startHint,
                limit + 1,
                [&visitData, &count, &marker, &limit, &nextHint](
                    std::shared_ptr<SLE const> const& sleCur) {
                    if (!sleCur)
                    {
                        // LCOV_EXCL_START
                        UNREACHABLE("xrpl::doAccountLines : null SLE");
                        return false;
                        // LCOV_EXCL_STOP
                    }

                    if (++count == limit)
                    {
                        marker = sleCur->key();
                        nextHint = RPC::getStartHint(sleCur, visitData.accountID);
                    }

                    if (sleCur->getType() != ltRIPPLE_STATE)
                        return true;

                    bool ignore = false;
                    if (visitData.ignoreDefault)
                    {
                        if (sleCur->getFieldAmount(sfLowLimit).getIssuer() == visitData.accountID)
                        {
                            ignore = !(sleCur->getFieldU32(sfFlags) & lsfLowReserve);
                        }
                        else
                        {
                            ignore = !(sleCur->getFieldU32(sfFlags) & lsfHighReserve);
                        }
                    }

                    if (!ignore && count <= limit)
                    {
                        auto const line = RPCTrustLine::makeItem(visitData.accountID, sleCur);

                        if (line &&
                            (!visitData.raPeerAccount ||
                             *visitData.raPeerAccount == line->getAccountIDPeer()))
                        {
                            visitData.items.emplace_back(*line);
                        }
                    }

                    return true;
                }))
        {
            return rpcError(RpcInvalidParams);
        }
    }

    // Emit a marker only when both: count reached limit+1 (more items exist)
    // and the marker key was actually recorded (guards the exact-boundary case).
    if (count == limit + 1 && marker)
    {
        result[jss::limit] = limit;
        result[jss::marker] = to_string(*marker) + "," + std::to_string(nextHint);
    }

    result[jss::account] = toBase58(accountID);

    for (auto const& item : visitData.items)
        addLine(jsonLines, item);

    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;
    return result;
}

}  // namespace xrpl
