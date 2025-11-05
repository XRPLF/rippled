#include <xrpld/app/main/Application.h>
#include <xrpld/app/paths/TrustLine.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

// Query:
// 1) Specify ledger to query.
// 2) Specify issuer account (cold wallet) in "account" field.
// 3) Specify accounts that hold gateway assets (such as hot wallets)
//    using "hotwallet" field which should be either a string (if just
//    one wallet) or an array of strings (if more than one).

// Response:
// 1) Array, "obligations", indicating the total obligations of the
//    gateway in each currency. Obligations to specified hot wallets
//    are not counted here.
// 2) Object, "balances", indicating balances in each account
//    that holds gateway assets. (Those specified in the "hotwallet"
//    field.)
// 3) Object of "assets" indicating accounts that owe the gateway.
//    (Gateways typically do not hold positive balances. This is unusual.)

// gateway_balances [<ledger>] <account> [<howallet> [<hotwallet [...

Json::Value
doGatewayBalances(RPC::JsonContext& context)
{
    auto& params = context.params;

    // Get the current ledger
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    if (!(params.isMember(jss::account) || params.isMember(jss::ident)))
        return RPC::missing_field_error(jss::account);

    std::string const strIdent(
        params.isMember(jss::account) ? params[jss::account].asString()
                                      : params[jss::ident].asString());

    // Get info on account.
    auto id = parseBase58<AccountID>(strIdent);
    if (!id)
        return rpcError(rpcACT_MALFORMED);
    auto const accountID{std::move(id.value())};
    context.loadType = Resource::feeHeavyBurdenRPC;

    result[jss::account] = toBase58(accountID);

    if (context.apiVersion > 1u && !ledger->exists(keylet::account(accountID)))
    {
        RPC::inject_error(rpcACT_NOT_FOUND, result);
        return result;
    }

    // Parse the specified hotwallet(s), if any
    std::set<AccountID> hotWallets;

    if (params.isMember(jss::hotwallet))
    {
        auto addHotWallet = [&hotWallets](Json::Value const& j) {
            if (j.isString())
            {
                if (auto id = parseBase58<AccountID>(j.asString()); id)
                {
                    hotWallets.insert(std::move(id.value()));
                    return true;
                }
            }

            return false;
        };

        Json::Value const& hw = params[jss::hotwallet];
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
            // The documentation states that invalidParams is used when
            // One or more fields are specified incorrectly.
            // invalidHotwallet should be used when the account exists, but does
            // not have currency issued by the account from the request.
            if (context.apiVersion < 2u)
            {
                RPC::inject_error(rpcINVALID_HOTWALLET, result);
            }
            else
            {
                RPC::inject_error(rpcINVALID_PARAMS, result);
            }
            return result;
        }
    }

    std::map<Currency, STAmount> sums;
    std::map<AccountID, std::vector<STAmount>> hotBalances;
    std::map<AccountID, std::vector<STAmount>> assets;
    std::map<AccountID, std::vector<STAmount>> frozenBalances;
    std::map<Currency, STAmount> locked;

    // Traverse the cold wallet's trust lines
    {
        forEachItem(
            *ledger, accountID, [&](std::shared_ptr<SLE const> const& sle) {
                if (sle->getType() == ltESCROW)
                {
                    auto const& escrow = sle->getFieldAmount(sfAmount);
                    auto& bal = locked[escrow.getCurrency()];
                    if (bal == beast::zero)
                    {
                        // This is needed to set the currency code correctly
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
                            // Presumably the exception was caused by overflow.
                            // On overflow return the largest valid STAmount.
                            // Very large sums of STAmount are approximations
                            // anyway.
                            bal = STAmount(
                                bal.issue(),
                                STAmount::cMaxValue,
                                STAmount::cMaxOffset);
                        }
                    }
                }

                auto rs = PathFindTrustLine::makeItem(accountID, sle);

                if (!rs)
                    return;

                int balSign = rs->getBalance().signum();
                if (balSign == 0)
                    return;

                auto const& peer = rs->getAccountIDPeer();

                // Here, a negative balance means the cold wallet owes (normal)
                // A positive balance means the cold wallet has an asset
                // (unusual)

                if (hotWallets.count(peer) > 0)
                {
                    // This is a specified hot wallet
                    hotBalances[peer].push_back(-rs->getBalance());
                }
                else if (balSign > 0)
                {
                    // This is a gateway asset
                    assets[peer].push_back(rs->getBalance());
                }
                else if (rs->getFreeze())
                {
                    // An obligation the gateway has frozen
                    frozenBalances[peer].push_back(-rs->getBalance());
                }
                else
                {
                    // normal negative balance, obligation to customer
                    auto& bal = sums[rs->getBalance().getCurrency()];
                    if (bal == beast::zero)
                    {
                        // This is needed to set the currency code correctly
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
                            // Presumably the exception was caused by overflow.
                            // On overflow return the largest valid STAmount.
                            // Very large sums of STAmount are approximations
                            // anyway.
                            bal = STAmount(
                                bal.issue(),
                                STAmount::cMaxValue,
                                STAmount::cMaxOffset);
                        }
                    }
                }
            });
    }

    if (!sums.empty())
    {
        Json::Value j;
        for (auto const& [k, v] : sums)
        {
            j[to_string(k)] = v.getText();
        }
        result[jss::obligations] = std::move(j);
    }

    auto populateResult =
        [&result](
            std::map<AccountID, std::vector<STAmount>> const& array,
            Json::StaticString const& name) {
            if (!array.empty())
            {
                Json::Value j;
                for (auto const& [accId, accBalances] : array)
                {
                    Json::Value balanceArray;
                    for (auto const& balance : accBalances)
                    {
                        Json::Value entry;
                        entry[jss::currency] =
                            to_string(balance.issue().currency);
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

    // Add total escrow to the result
    if (!locked.empty())
    {
        Json::Value j;
        for (auto const& [k, v] : locked)
        {
            j[to_string(k)] = v.getText();
        }
        result[jss::locked] = std::move(j);
    }

    return result;
}

}  // namespace ripple
