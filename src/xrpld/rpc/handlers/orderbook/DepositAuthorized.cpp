#include <xrpld/rpc/handlers/orderbook/DepositAuthorized.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/SpecBridge.hpp>

#include <xrpl/basics/Slice.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/jss.h>

#include <rpcspec/Errors.hpp>

#include <expected>
#include <set>
#include <utility>
#include <vector>

namespace xrpl::rpc {

DepositAuthorizedHandler::DepositAuthorizedHandler(JsonContext& context) : context_(context)
{
}

std::expected<DepositAuthorizedHandler::Output, ::rpc::Status>
DepositAuthorizedHandler::process(Input const& input) const
{
    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context_.get());
        status != ::rpc::Status::kOK)
    {
        return std::unexpected{status};
    }
    auto const& ledger = *output.ledger;

    auto const& srcAccount = input.sourceAccount;
    auto const& dstAccount = input.destinationAccount;

    // If source account is not in the ledger it can't be authorized.
    if (not ledger.exists(keylet::account(srcAccount)))
    {
        output.error = RpcSrcActNotFound;
        return output;
    }

    // If destination account is not in the ledger you can't deposit to it, eh?
    auto const sleDest = ledger.read(keylet::account(dstAccount));
    if (!sleDest)
    {
        output.error = RpcDstActNotFound;
        return output;
    }

    bool const reqAuth = sleDest->isFlag(lsfDepositAuth) && (srcAccount != dstAccount);

    std::set<std::pair<AccountID, Slice>> sorted;
    std::vector<SLE::const_pointer> lifeExtender;
    if (input.credentials.has_value())
    {
        lifeExtender.reserve(input.credentials->size());
        for (auto const& credH : *input.credentials)
        {
            // A zero key names an entry that cannot exist
            SLE::const_pointer sleCred =
                credH.isZero() ? nullptr : ledger.read(keylet::credential(credH));
            if (!sleCred)
            {
                output.error = ::rpc::Status{RpcBadCredentials, "credentials don't exist"};
                return output;
            }

            if (!sleCred->isFlag(lsfAccepted))
            {
                output.error = ::rpc::Status{RpcBadCredentials, "credentials aren't accepted"};
                return output;
            }

            if (credentials::checkExpired(*sleCred, ledger.header().parentCloseTime))
            {
                output.error = ::rpc::Status{RpcBadCredentials, "credentials are expired"};
                return output;
            }

            if ((*sleCred)[sfSubject] != srcAccount)
            {
                output.error = ::rpc::Status{
                    RpcBadCredentials,
                    "credentials doesn't belong to the root account",
                };
                return output;
            }

            auto [it, ins] = sorted.emplace((*sleCred)[sfIssuer], (*sleCred)[sfCredentialType]);
            if (!ins)
            {
                output.error = ::rpc::Status{RpcBadCredentials, "duplicates in credentials"};
                return output;
            }
            lifeExtender.push_back(std::move(sleCred));
        }
    }

    // If the two accounts are the same OR if that flag is
    // not set, then the deposit should be fine.
    output.depositAuthorized = true;
    if (reqAuth)
    {
        output.depositAuthorized = ledger.exists(keylet::depositPreauth(dstAccount, srcAccount)) or
            (input.credentials.has_value() and
             ledger.exists(keylet::depositPreauth(dstAccount, sorted)));
    }

    return output;
}

void
DepositAuthorizedHandler::writeResult(json::Value& value, Output const& output) const
{
    auto const& params = context_.get().params;

    injectLedgerFields(*output.ledger, context_.get(), value);

    if (output.error.has_value())
    {
        injectSpecError(value, *output.error);
        return;
    }

    value[jss::source_account] = params[jss::source_account].asString();
    value[jss::destination_account] = params[jss::destination_account].asString();
    if (params.isMember(jss::credentials))
        value[jss::credentials] = params[jss::credentials];

    value[jss::deposit_authorized] = output.depositAuthorized;
}

}  // namespace xrpl::rpc
