#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/LoadFeeTrack.h>

#include <cstdint>
#include <memory>

namespace xrpl {

static void
fillTransaction(
    RPC::JsonContext& context,
    Json::Value& txArray,
    AccountID const& accountID,
    std::uint32_t& sequence,
    ReadView const& ledger)
{
    txArray[jss::Sequence] = Json::UInt(sequence++);
    txArray[jss::Account] = toBase58(accountID);
    auto& fees = ledger.fees();
    // Convert the reference transaction cost in fee units to drops
    // scaled to represent the current fee load.
    txArray[jss::Fee] =
        scaleFeeLoad(fees.base, context.app.getFeeTrack(), fees, false).jsonClipped();
}

// {
//   account: <account>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional, number of problems
//   role: gateway|user             // account role to assume
//   transactions: true             // optional, recommend transactions
// }
Json::Value
doNoRippleCheck(RPC::JsonContext& context)
{
    auto const& params(context.params);

    // check account param
    if (!params.isMember(jss::account))
        return RPC::missing_field_error(jss::account);

    if (!params[jss::account].isString())
        return RPC::invalid_field_error(jss::account);

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        return rpcError(rpcACT_MALFORMED);
    }
    auto const accountID{std::move(id.value())};

    // check role param
    if (!params.isMember(jss::role))
        return RPC::missing_field_error(jss::role);

    bool roleGateway = false;
    {
        std::string const role = params[jss::role].asString();
        if (role == jss::gateway)
            roleGateway = true;
        else if (role != jss::user)
            return RPC::invalid_field_error(jss::role);
    }

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::noRippleCheck, context))
        return *err;

    // check transactions param
    // The document[https://xrpl.org/noripple_check.html#noripple_check] states
    // that transactions params is a boolean value, however, assigning any
    // string value works. Do not allow this. This check is for api Version 2
    // onwards only
    if (context.apiVersion > 1u && params.isMember(jss::transactions) &&
        !params[jss::transactions].isBool())
    {
        return RPC::invalid_field_error(jss::transactions);
    }

    bool transactions = false;
    if (params.isMember(jss::transactions))
        transactions = params[jss::transactions].asBool();

    // lookup ledger via params
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto const sle = ledger->read(keylet::account(accountID));
    if (!sle)
        return rpcError(rpcACT_NOT_FOUND);

    std::uint32_t seq = sle->getFieldU32(sfSequence);

    Json::Value& problems = (result[jss::problems] = Json::arrayValue);

    bool const defaultRipple = sle->getFieldU32(sfFlags) & lsfDefaultRipple;

    Json::Value jvTransactions = Json::arrayValue;

    if (defaultRipple && !roleGateway)
    {
        problems.append(
            "You appear to have set your default ripple flag even though you "
            "are not a gateway. This is not recommended unless you are "
            "experimenting");
    }
    else if (roleGateway && !defaultRipple)
    {
        problems.append("You should immediately set your default ripple flag");
        if (transactions)
        {
            Json::Value& tx = jvTransactions.append(Json::objectValue);
            tx[jss::TransactionType] = jss::AccountSet;
            tx[jss::SetFlag] = 8;
            fillTransaction(context, tx, accountID, seq, *ledger);
        }
    }

    forEachItemAfter(
        *ledger, accountID, uint256(), 0, limit, [&](std::shared_ptr<SLE const> const& ownedItem) {
            if (ownedItem->getType() == ltRIPPLE_STATE)
            {
                bool const low = accountID == ownedItem->getFieldAmount(sfLowLimit).getIssuer();

                bool const noRipple =
                    ownedItem->getFieldU32(sfFlags) & (low ? lsfLowNoRipple : lsfHighNoRipple);

                std::string problem;
                bool needFix = false;
                if (noRipple && roleGateway)
                {
                    problem = "You should clear the no ripple flag on your ";
                    needFix = true;
                }
                else if (!roleGateway && !noRipple)
                {
                    problem = "You should probably set the no ripple flag on your ";
                    needFix = true;
                }
                if (needFix)
                {
                    AccountID const peer =
                        ownedItem->getFieldAmount(low ? sfHighLimit : sfLowLimit).getIssuer();
                    STAmount const peerLimit =
                        ownedItem->getFieldAmount(low ? sfHighLimit : sfLowLimit);
                    problem += to_string(peerLimit.get<Issue>().currency);
                    problem += " line to ";
                    problem += to_string(peerLimit.getIssuer());
                    problems.append(problem);

                    STAmount limitAmount(ownedItem->getFieldAmount(low ? sfLowLimit : sfHighLimit));
                    limitAmount.get<Issue>().account = peer;

                    if (transactions)
                    {
                        Json::Value& tx = jvTransactions.append(Json::objectValue);
                        tx[jss::TransactionType] = jss::TrustSet;
                        tx[jss::LimitAmount] = limitAmount.getJson(JsonOptions::none);
                        tx[jss::Flags] = noRipple ? tfClearNoRipple : tfSetNoRipple;
                        fillTransaction(context, tx, accountID, seq, *ledger);
                    }

                    return true;
                }
            }
            return false;
        });

    if (transactions)
        result[jss::transactions] = std::move(jvTransactions);
    return result;
}

}  // namespace xrpl
