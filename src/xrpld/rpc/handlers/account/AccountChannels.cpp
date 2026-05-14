/** @file
 *  Implements the `account_channels` RPC handler.
 *
 *  Returns the set of payment channels (`ltPAYCHAN`) for which a given account
 *  is the source (funding) party. The result is drawn from a read-only
 *  `ReadView` snapshot of the ledger; no state is modified. Supports
 *  cursor-based pagination using the shared `"<uint256_hex>,<uint64_hint>"`
 *  compound-marker format.
 */
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
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
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/resource/Fees.h>

#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** Serialize a single payment channel SLE into a JSON object and append it to
 *  an array.
 *
 *  Mandatory fields (`channel_id`, `account`, `destination_account`, `amount`,
 *  `balance`, `settle_delay`) are always emitted. Optional fields
 *  (`expiration`, `cancel_after`, `source_tag`, `destination_tag`) are
 *  included only when present in the SLE.
 *
 *  The public key is emitted in both base58 (`public_key`) and hex
 *  (`public_key_hex`) forms only when `publicKeyType()` confirms that the raw
 *  bytes in `sfPublicKey` represent a recognized key type, guarding against
 *  malformed or zero-length field values.
 *
 *  @param jsonLines  JSON array to which the new channel object is appended.
 *  @param line       Resolved SLE of type `ltPAYCHAN` to serialize.
 */
void
addChannel(json::Value& jsonLines, SLE const& line)
{
    json::Value& jDst(jsonLines.append(json::ValueType::Object));
    jDst[jss::channel_id] = to_string(line.key());
    jDst[jss::account] = to_string(line[sfAccount]);
    jDst[jss::destination_account] = to_string(line[sfDestination]);
    jDst[jss::amount] = line[sfAmount].getText();
    jDst[jss::balance] = line[sfBalance].getText();
    if (publicKeyType(line[sfPublicKey]))
    {
        PublicKey const pk(line[sfPublicKey]);
        jDst[jss::public_key] = toBase58(TokenType::AccountPublic, pk);
        jDst[jss::public_key_hex] = strHex(pk);
    }
    jDst[jss::settle_delay] = line[sfSettleDelay];
    if (auto const& v = line[~sfExpiration])
        jDst[jss::expiration] = *v;
    if (auto const& v = line[~sfCancelAfter])
        jDst[jss::cancel_after] = *v;
    if (auto const& v = line[~sfSourceTag])
        jDst[jss::source_tag] = *v;
    if (auto const& v = line[~sfDestinationTag])
        jDst[jss::destination_tag] = *v;
}

/** Handle the `account_channels` RPC command.
 *
 *  Returns all payment channels where `account` is the source (funding) party.
 *  Channels where the queried account is only the destination do not appear
 *  here; they live in the source account's owner directory.
 *
 *  Pagination uses a compound marker of the form `"<uint256_hex>,<uint64_hint>"`.
 *  The uint256 is the ledger key of the last-seen SLE; the uint64 is a page
 *  hint that lets `forEachItemAfter` jump directly to the right owner-directory
 *  page rather than scanning from the beginning. A marker is only included in
 *  the response when a subsequent item is confirmed to exist, preventing
 *  empty-page follow-up requests.
 *
 *  Before resuming a paginated walk, the SLE pointed to by the supplied marker
 *  is verified to belong to `account` via `RPC::isRelatedToAccount`. A marker
 *  pointing into a different account's namespace is rejected with
 *  `rpcINVALID_PARAMS`.
 *
 *  Sets `context.loadType` to `feeMediumBurdenRPC` because owner-directory
 *  traversal over potentially hundreds of items is moderately expensive.
 *
 *  @param context  RPC dispatch context carrying params, ledger access, and
 *      resource-management hooks.
 *  @return JSON object containing a `channels` array (each element produced by
 *      `addChannel`), the echoed `account` field, and an optional `marker` and
 *      `limit` when more results remain.
 */
json::Value
doAccountChannels(RPC::JsonContext& context)
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
        return rpcError(RpcActMalformed);
    }
    AccountID const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(RpcActNotFound);

    std::string strDst;
    if (params.isMember(jss::destination_account))
    {
        if (!params[jss::destination_account].isString())
            return RPC::invalidFieldError(jss::destination_account);
        strDst = params[jss::destination_account].asString();
    }

    auto const raDstAccount = [&]() -> std::optional<AccountID> {
        return strDst.empty() ? std::nullopt : parseBase58<AccountID>(strDst);
    }();
    if (!strDst.empty() && !raDstAccount)
        return rpcError(RpcActMalformed);

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kACCOUNT_CHANNELS, context))
        return *err;

    json::Value jsonChannels{json::ValueType::Array};
    /** Closure state threaded through the `forEachItemAfter` callback.
     *
     *  Holds the accumulating result set and the filter criteria used to
     *  accept or skip each SLE visited in the owner directory.
     */
    struct VisitData
    {
        std::vector<std::shared_ptr<SLE const>> items;
        AccountID const& accountID;
        std::optional<AccountID> const& raDstAccount;
    };
    VisitData visitData = {.items = {}, .accountID = accountID, .raDstAccount = raDstAccount};
    visitData.items.reserve(limit);
    uint256 startAfter = beast::kZERO;
    std::uint64_t startHint = 0;

    if (params.isMember(jss::marker))
    {
        if (!params[jss::marker].isString())
            return RPC::expectedFieldError(jss::marker, "string");

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

        auto const sle = ledger->read({ltANY, startAfter});

        if (!sle)
            return rpcError(RpcInvalidParams);

        if (!RPC::isRelatedToAccount(*ledger, sle, accountID))
            return rpcError(RpcInvalidParams);
    }

    auto count = 0;
    std::optional<uint256> marker = {};
    std::uint64_t nextHint = 0;
    if (!forEachItemAfter(
            *ledger,
            accountID,
            startAfter,
            startHint,
            limit + 1,
            [&visitData, &accountID, &count, &limit, &marker, &nextHint](
                std::shared_ptr<SLE const> const& sleCur) {
                if (!sleCur)
                {
                    // LCOV_EXCL_START
                    UNREACHABLE("xrpl::doAccountChannels : null SLE");
                    return false;
                    // LCOV_EXCL_STOP
                }

                if (++count == limit)
                {
                    marker = sleCur->key();
                    nextHint = RPC::getStartHint(sleCur, visitData.accountID);
                }

                if (count <= limit && sleCur->getType() == ltPAYCHAN &&
                    (*sleCur)[sfAccount] == accountID &&
                    (!visitData.raDstAccount ||
                     *visitData.raDstAccount == (*sleCur)[sfDestination]))
                {
                    visitData.items.emplace_back(sleCur);
                }

                return true;
            }))
    {
        return rpcError(RpcInvalidParams);
    }

    if (count == limit + 1 && marker)
    {
        result[jss::limit] = limit;
        result[jss::marker] = to_string(*marker) + "," + std::to_string(nextHint);
    }

    result[jss::account] = toBase58(accountID);

    for (auto const& item : visitData.items)
        addChannel(jsonChannels, *item);

    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;
    result[jss::channels] = std::move(jsonChannels);
    return result;
}

}  // namespace xrpl
