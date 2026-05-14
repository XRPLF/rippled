#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

/** @file
 *  Implements the `gateway_balances` RPC handler, providing a financial
 *  snapshot for XRPL token issuers (gateways) operating a cold/hot wallet
 *  architecture.
 */

/** Produce a financial snapshot for an XRPL gateway (token issuer).
 *
 *  Traverses the cold wallet's owner directory and classifies each trust
 *  line and escrow into one of five output buckets:
 *
 *  - `obligations` — aggregate liabilities per currency to all normal
 *    (non-hot, non-frozen) customer accounts.
 *  - `balances` — per-hot-wallet balances for the accounts listed in the
 *    `hotwallet` request field.
 *  - `frozen_balances` — per-peer balances on trust lines the gateway has
 *    frozen.
 *  - `assets` — per-peer positive balances (the gateway is owed; unusual).
 *  - `locked` — aggregate IOU/XRP amounts locked in escrow per currency.
 *    MPT-denominated escrows are excluded.
 *
 *  The `hotwallet` parameter accepts a single Base58-encoded account string,
 *  an array of such strings, or null (treated as an empty set). An invalid
 *  entry returns `rpcINVALID_HOTWALLET` on API v1 and `rpcINVALID_PARAMS`
 *  on API v2+. Account existence is only enforced for API v2+; v1 returns an
 *  empty result for a nonexistent account to preserve backward compatibility.
 *
 *  Arithmetic overflow during `obligations` or `locked` accumulation is
 *  caught and the affected bucket is clamped to `STAmount::cMaxValue /
 *  cMaxOffset` rather than failing the call — very large sums are
 *  approximations regardless.
 *
 *  @note This handler declares `feeHeavyBurdenRPC` because traversing the
 *      owner directory is O(n) in the number of trust lines; large gateways
 *      can have thousands.
 *
 *  @param context The RPC dispatch context carrying the request parameters,
 *      ledger access, role, and API version.
 *  @return A JSON object with any non-empty combination of `obligations`,
 *      `balances`, `frozen_balances`, `assets`, and `locked` keys.
 */
json::Value
doGatewayBalances(RPC::JsonContext& context)
{
    auto& params = context.params;

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    if (!(params.isMember(jss::account) || params.isMember(jss::ident)))
        return RPC::missingFieldError(jss::account);

    std::string const strIdent(
        params.isMember(jss::account) ? params[jss::account].asString()
                                      : params[jss::ident].asString());

    auto id = parseBase58<AccountID>(strIdent);
    if (!id)
        return rpcError(RpcActMalformed);
    auto const accountID{id.value()};
    context.loadType = Resource::kFEE_HEAVY_BURDEN_RPC;

    result[jss::account] = toBase58(accountID);

    if (context.apiVersion > 1u && !ledger->exists(keylet::account(accountID)))
    {
        RPC::injectError(RpcActNotFound, result);
        return result;
    }

    // Parse the specified hotwallet(s), if any
    std::set<AccountID> hotWallets;

    if (params.isMember(jss::hotwallet))
    {
        /** Parses a single hotwallet entry and inserts it into the set.
         *  Returns false if @p j is not a valid Base58 account string. */
        auto addHotWallet = [&hotWallets](json::Value const& j) {
            if (j.isString())
            {
                if (auto id = parseBase58<AccountID>(j.asString()); id)
                {
                    hotWallets.insert(id.value());
                    return true;
                }
            }

            return false;
        };

        json::Value const& hw = params[jss::hotwallet];
        bool valid = true;

        // null is treated as a valid 0-sized array of hotwallet
        if (hw.isArrayOrNull())
        {
            for (unsigned i = 0; i < hw.size(); ++i)
                valid &= addHotWallet(hw[i]);
        }
        else if (hw.isString())
        {
            valid &= addHotWallet(hw);
        }
        else
        {
            valid = false;
        }

        if (!valid)
        {
            // v1 used the domain-specific rpcINVALID_HOTWALLET code; v2+
            // normalised to rpcINVALID_PARAMS for parameter-validation errors.
            if (context.apiVersion < 2u)
            {
                RPC::injectError(RpcInvalidHotwallet, result);
            }
            else
            {
                RPC::injectError(RpcInvalidParams, result);
            }
            return result;
        }
    }

    std::map<Currency, STAmount> sums;
    std::map<AccountID, std::vector<STAmount>> hotBalances;
    std::map<AccountID, std::vector<STAmount>> assets;
    std::map<AccountID, std::vector<STAmount>> frozenBalances;
    std::map<Currency, STAmount> locked;

    // Traverse the cold wallet's owner directory, classifying each SLE.
    forEachItem(*ledger, accountID, [&](std::shared_ptr<SLE const> const& sle) {
        if (sle->getType() == ltESCROW)
        {
            auto const& escrow = sle->getFieldAmount(sfAmount);
            // MPT escrows are excluded: the locked-funds model predates MPTs.
            if (escrow.holds<MPTIssue>())
                return;

            auto& bal = locked[escrow.get<Issue>().currency];
            if (bal == beast::kZERO)
            {
                // Direct assignment on the first entry sets the currency code;
                // adding to a default-constructed zero would not.
                bal = escrow;
            }
            else
            {
                try
                {
                    bal += escrow;
                }
                catch (std::runtime_error const&)
                {
                    // Overflow: clamp to the maximum representable STAmount.
                    // Very large sums are approximations regardless.
                    bal =
                        STAmount(bal.get<Issue>(), STAmount::kMAX_VALUE, STAmount::kMAX_OFFSET);
                }
            }
        }

        auto rs = PathFindTrustLine::makeItem(accountID, sle);

        if (!rs)
            return;

        int const balSign = rs->getBalance().signum();
        if (balSign == 0)
            return;

        auto const& peer = rs->getAccountIDPeer();

        // Negative balance: the cold wallet owes the peer (normal issuer
        // liability). Positive balance: the cold wallet holds an asset
        // (unusual — peer owes the gateway).

        if (hotWallets.contains(peer))
        {
            hotBalances[peer].push_back(-rs->getBalance());
        }
        else if (balSign > 0)
        {
            assets[peer].push_back(rs->getBalance());
        }
        else if (rs->getFreeze())
        {
            frozenBalances[peer].push_back(-rs->getBalance());
        }
        else
        {
            // Aggregate normal liability by currency.
            auto& bal = sums[rs->getBalance().get<Issue>().currency];
            if (bal == beast::kZERO)
            {
                // Direct assignment on the first entry sets the currency code.
                bal = -rs->getBalance();
            }
            else
            {
                try
                {
                    bal -= rs->getBalance();
                }
                catch (std::runtime_error const&)
                {
                    // Overflow: clamp to the maximum representable STAmount.
                    // Very large sums are approximations regardless.
                    bal = STAmount(bal.asset(), STAmount::kMAX_VALUE, STAmount::kMAX_OFFSET);
                }
            }
        }
    });

    if (!sums.empty())
    {
        json::Value j;
        for (auto const& [k, v] : sums)
        {
            j[to_string(k)] = v.getText();
        }
        result[jss::obligations] = std::move(j);
    }

    /** Serialises a per-account balance map into @p result under key @p name.
     *  Omits the key entirely when @p array is empty, keeping the response
     *  compact. Each account entry is an array of {currency, value} objects. */
    auto populateResult = [&result](
                              std::map<AccountID, std::vector<STAmount>> const& array,
                              json::StaticString const& name) {
        if (!array.empty())
        {
            json::Value j;
            for (auto const& [accId, accBalances] : array)
            {
                json::Value balanceArray;
                for (auto const& balance : accBalances)
                {
                    json::Value entry;
                    entry[jss::currency] = to_string(balance.get<Issue>().currency);
                    entry[jss::value] = balance.getText();
                    balanceArray.append(std::move(entry));
                }
                j[to_string(accId)] = std::move(balanceArray);
            }
            result[name] = std::move(j);
        }
    };

    populateResult(hotBalances, jss::balances);
    populateResult(frozenBalances, jss::frozen_balances);
    populateResult(assets, jss::assets);

    if (!locked.empty())
    {
        json::Value j;
        for (auto const& [k, v] : locked)
        {
            j[to_string(k)] = v.getText();
        }
        result[jss::locked] = std::move(j);
    }

    return result;
}

}  // namespace xrpl
