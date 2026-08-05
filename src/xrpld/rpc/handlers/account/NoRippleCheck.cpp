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
    json::Value& txArray,
    AccountID const& accountID,
    std::uint32_t& sequence,
    ReadView const& ledger)
{
    txArray["Sequence"] = json::UInt(sequence++);
    txArray["Account"] = toBase58(accountID);
    auto& fees = ledger.fees();
    // Convert the reference transaction cost in fee units to drops
    // scaled to represent the current fee load.
    txArray["Fee"] = scaleFeeLoad(fees.base, context.app.getFeeTrack(), fees, false).jsonClipped();
}

// {
//   account: <account>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional, number of problems
//   role: gateway|user             // account role to assume
//   transactions: true             // optional, recommend transactions
// }
json::Value
doNoRippleCheck(RPC::JsonContext& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::account))
        return RPC::missingFieldError("account");

    if (!params.isMember("role"))
        return RPC::missingFieldError("role");

    if (!params[jss::account].isString())
        return RPC::invalidFieldError(jss::account);

    bool roleGateway = false;
    {
        std::string const role = params["role"].asString();
        if (role == "gateway")
        {
            roleGateway = true;
        }
        else if (role != "user")
        {
            return RPC::invalidFieldError("role");
        }
    }

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kNoRippleCheck, context))
        return *err;

    bool transactions = false;
    if (params.isMember(jss::transactions))
        transactions = params["transactions"].asBool();

    // The document[https://xrpl.org/noripple_check.html#noripple_check] states
    // that transactions params is a boolean value, however, assigning any
    // string value works. Do not allow this. This check is for api Version 2
    // onwards only
    if (context.apiVersion > 1u && params.isMember(jss::transactions) &&
        !params[jss::transactions].isBool())
    {
        return RPC::invalidFieldError(jss::transactions);
    }

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    json::Value dummy;  // NOLINT(misc-const-correctness)
    json::Value& jvTransactions =
        transactions ? (result[jss::transactions] = json::ValueType::Array) : dummy;

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::injectError(RpcActMalformed, result);
        return result;
    }
    auto const accountID{id.value()};
    auto const sle = ledger->read(keylet::account(accountID));
    if (!sle)
        return rpcError(RpcActNotFound);

    std::uint32_t seq = sle->getFieldU32(sfSequence);

    json::Value& problems = (result["problems"] = json::ValueType::Array);

    bool const bDefaultRipple = sle->isFlag(lsfDefaultRipple);

    if (bDefaultRipple && !roleGateway)
    {
        problems.append(
            "You appear to have set your default ripple flag even though you "
            "are not a gateway. This is not recommended unless you are "
            "experimenting");
    }
    else if (roleGateway && !bDefaultRipple)
    {
        problems.append("You should immediately set your default ripple flag");
        if (transactions)
        {
            json::Value& tx = jvTransactions.append(json::ValueType::Object);
            tx["TransactionType"] = jss::AccountSet;
            tx["SetFlag"] = 8;
            fillTransaction(context, tx, accountID, seq, *ledger);
        }
    }

    forEachItemAfter(*ledger, accountID, uint256(), 0, limit, [&](SLE::const_ref ownedItem) {
        if (ownedItem->getType() == ltRIPPLE_STATE)
        {
            bool const bLow = accountID == ownedItem->getFieldAmount(sfLowLimit).getIssuer();

            bool const bNoRipple = ownedItem->isFlag(bLow ? lsfLowNoRipple : lsfHighNoRipple);

            std::string problem;
            bool needFix = false;
            if (bNoRipple && roleGateway)
            {
                problem = "You should clear the no ripple flag on your ";
                needFix = true;
            }
            else if (!roleGateway && !bNoRipple)
            {
                problem = "You should probably set the no ripple flag on your ";
                needFix = true;
            }
            if (needFix)
            {
                AccountID const peer =
                    ownedItem->getFieldAmount(bLow ? sfHighLimit : sfLowLimit).getIssuer();
                STAmount const peerLimit =
                    ownedItem->getFieldAmount(bLow ? sfHighLimit : sfLowLimit);
                problem += to_string(peerLimit.get<Issue>().currency);
                problem += " line to ";
                problem += to_string(peerLimit.getIssuer());
                problems.append(problem);

                STAmount limitAmount(ownedItem->getFieldAmount(bLow ? sfLowLimit : sfHighLimit));
                limitAmount.get<Issue>().account = peer;

                json::Value& tx = jvTransactions.append(json::ValueType::Object);
                tx["TransactionType"] = jss::TrustSet;
                tx["LimitAmount"] = limitAmount.getJson(JsonOptions::Values::None);
                tx["Flags"] = bNoRipple ? tfClearNoRipple : tfSetNoRipple;
                fillTransaction(context, tx, accountID, seq, *ledger);

                return true;
            }
        }
        return false;
    });

    return result;
}

}  // namespace xrpl
