#include <xrpld/rpc/handlers/ledger/Ledger.h>

#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/shamap/SHAMap.h>

#include <grpcpp/support/status.h>
#include <org/xrpl/rpc/v1/get_ledger.pb.h>

#include <chrono>
#include <exception>
#include <limits>
#include <memory>
#include <utility>

namespace xrpl {
namespace RPC {

LedgerHandler::LedgerHandler(JsonContext& context) : context_(context)
{
}

Status
LedgerHandler::check()
{
    auto const& params = context_.params;

    auto getBool = [&](json::StaticString const& field) -> Expected<bool, Status> {
        if (!params.isMember(field))
        {
            return false;
        }
        if (!params[field].isBool())
        {
            return Unexpected(RpcInvalidParams);
        }

        return params[field].asBool();
    };

    auto const full = getBool(jss::full);
    auto const transactions = getBool(jss::transactions);
    auto const accounts = getBool(jss::accounts);
    auto const expand = getBool(jss::expand);
    auto const binary = getBool(jss::binary);
    auto const ownerFunds = getBool(jss::owner_funds);
    auto const queue = getBool(jss::queue);

    if (!full.has_value())
        return full.error();
    if (!transactions.has_value())
        return transactions.error();
    if (!accounts.has_value())
        return accounts.error();
    if (!expand.has_value())
        return expand.error();
    if (!binary.has_value())
        return binary.error();
    if (!ownerFunds.has_value())
        return ownerFunds.error();
    if (!queue.has_value())
        return queue.error();

    options_ = (*full ? static_cast<int>(LedgerFill::Options::Full) : 0) |
        (*expand ? static_cast<int>(LedgerFill::Options::Expand) : 0) |
        (*transactions ? static_cast<int>(LedgerFill::Options::DumpTxrp) : 0) |
        (*accounts ? static_cast<int>(LedgerFill::Options::DumpState) : 0) |
        (*binary ? static_cast<int>(LedgerFill::Options::Binary) : 0) |
        (*ownerFunds ? static_cast<int>(LedgerFill::Options::OwnerFunds) : 0) |
        (*queue ? static_cast<int>(LedgerFill::Options::DumpQueue) : 0);

    bool const needsLedger = params.isMember(jss::ledger) || params.isMember(jss::ledger_hash) ||
        params.isMember(jss::ledger_index);
    if (!needsLedger)
        return Status::kOK;
    if (auto s = lookupLedger(ledger_, context_, result_))
        return s;

    if (*full || *accounts)
    {
        // Until some sane way to get full ledgers has been implemented,
        // disallow retrieving all state nodes.
        if (!isUnlimited(context_.role))
            return RpcNoPermission;

        if (context_.app.getFeeTrack().isLoadedLocal() && !isUnlimited(context_.role))
        {
            return RpcTooBusy;
        }
        context_.loadType = binary ? Resource::kFeeMediumBurdenRpc : Resource::kFeeHeavyBurdenRpc;
    }

    if (*queue)
    {
        if (!ledger_ || !ledger_->open())
        {
            // It doesn't make sense to request the queue
            // with a non-existent or closed/validated ledger.
            return RpcInvalidParams;
        }

        queueTxs_ = context_.app.getTxQ().getTxs();
    }

    return Status::kOK;
}

void
LedgerHandler::writeResult(json::Value& value)
{
    if (ledger_)
    {
        copyFrom(value, result_);
        addJson(value, {*ledger_, &context_, options_, queueTxs_});
    }
    else
    {
        auto& master = context_.app.getLedgerMaster();
        {
            auto& closed = value[jss::closed] = json::ValueType::Object;
            addJson(closed, {*master.getClosedLedger(), &context_, 0});
        }
        {
            auto& open = value[jss::open] = json::ValueType::Object;
            addJson(open, {*master.getCurrentLedger(), &context_, 0});
        }
    }

    json::Value warnings{json::ValueType::Array};
    if (context_.params.isMember(jss::type))
    {
        json::Value& w = warnings.append(json::ValueType::Object);
        w[jss::id] = WarnRpcFieldsDeprecated;
        w[jss::message] =
            "Some fields from your request are deprecated. Please check the "
            "documentation at "
            "https://xrpl.org/docs/references/http-websocket-apis/ "
            "and update your request. Field `type` is deprecated.";
    }

    if (warnings.size() != 0u)
        value[jss::warnings] = std::move(warnings);
}

}  // namespace RPC

std::pair<org::xrpl::rpc::v1::GetLedgerResponse, grpc::Status>
doLedgerGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest>& context)
{
    auto begin = std::chrono::system_clock::now();
    org::xrpl::rpc::v1::GetLedgerRequest const& request = context.params;
    org::xrpl::rpc::v1::GetLedgerResponse response;
    grpc::Status const status = grpc::Status::OK;

    std::shared_ptr<ReadView const> ledger;
    if (auto status = RPC::ledgerFromRequest(ledger, context))
    {
        grpc::Status errorStatus;
        if (status.toErrorCode() == RpcInvalidParams)
        {
            errorStatus = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, status.message());
        }
        else
        {
            errorStatus = grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
        }
        return {response, errorStatus};
    }

    Serializer s;
    addRaw(ledger->header(), s, true);

    response.set_ledger_header(s.peekData().data(), s.getLength());

    if (request.transactions())
    {
        try
        {
            for (auto& i : ledger->txs)
            {
                XRPL_ASSERT(i.first, "xrpl::doLedgerGrpc : non-null transaction");
                if (request.expand())
                {
                    auto txn = response.mutable_transactions_list()->add_transactions();
                    Serializer const sTxn = i.first->getSerializer();
                    txn->set_transaction_blob(sTxn.data(), sTxn.getLength());
                    if (i.second)
                    {
                        Serializer const sMeta = i.second->getSerializer();
                        txn->set_metadata_blob(sMeta.data(), sMeta.getLength());
                    }
                }
                else
                {
                    auto const& hash = i.first->getTransactionID();
                    response.mutable_hashes_list()->add_hashes(hash.data(), hash.size());
                }
            }
        }
        catch (std::exception const& e)
        {
            JLOG(context.j.error()) << __func__ << " - Error deserializing transaction in ledger "
                                    << ledger->header().seq
                                    << " . skipping transaction and following transactions. You "
                                       "should look into this further";
        }
    }

    if (request.get_objects())
    {
        std::shared_ptr<ReadView const> const parent =
            context.app.getLedgerMaster().getLedgerBySeq(ledger->seq() - 1);

        std::shared_ptr<Ledger const> const base = std::dynamic_pointer_cast<Ledger const>(parent);
        if (!base)
        {
            grpc::Status const errorStatus{
                grpc::StatusCode::NOT_FOUND, "parent ledger not validated"};
            return {response, errorStatus};
        }

        std::shared_ptr<Ledger const> const desired =
            std::dynamic_pointer_cast<Ledger const>(ledger);
        if (!desired)
        {
            grpc::Status const errorStatus{grpc::StatusCode::NOT_FOUND, "ledger not validated"};
            return {response, errorStatus};
        }
        SHAMap::Delta differences;

        int const maxDifferences = std::numeric_limits<int>::max();

        bool const res = base->stateMap().compare(desired->stateMap(), differences, maxDifferences);
        if (!res)
        {
            grpc::Status const errorStatus{
                grpc::StatusCode::RESOURCE_EXHAUSTED,
                "too many differences between specified ledgers"};
            return {response, errorStatus};
        }

        for (auto& [k, v] : differences)
        {
            auto obj = response.mutable_ledger_objects()->add_objects();
            auto inBase = v.first;
            auto inDesired = v.second;

            obj->set_key(k.data(), k.size());
            if (inDesired)
            {
                XRPL_ASSERT(inDesired->size() > 0, "xrpl::doLedgerGrpc : non-empty desired");
                obj->set_data(inDesired->data(), inDesired->size());
            }
            if (inBase && inDesired)
            {
                obj->set_mod_type(org::xrpl::rpc::v1::RawLedgerObject::MODIFIED);
            }
            else if (inBase && !inDesired)
            {
                obj->set_mod_type(org::xrpl::rpc::v1::RawLedgerObject::DELETED);
            }
            else
            {
                obj->set_mod_type(org::xrpl::rpc::v1::RawLedgerObject::CREATED);
            }
            auto const blob = inDesired ? inDesired->slice() : inBase->slice();
            auto const objectType = static_cast<LedgerEntryType>(blob[1] << 8 | blob[2]);

            if (request.get_object_neighbors())
            {
                if (!(inBase && inDesired))
                {
                    auto lb = desired->stateMap().lowerBound(k);
                    auto ub = desired->stateMap().upperBound(k);
                    if (lb != desired->stateMap().end())
                        obj->set_predecessor(lb->key().data(), lb->key().size());
                    if (ub != desired->stateMap().end())
                        obj->set_successor(ub->key().data(), ub->key().size());
                    if (objectType == ltDIR_NODE)
                    {
                        auto sle = std::make_shared<SLE>(SerialIter{blob}, k);
                        if (!sle->isFieldPresent(sfOwner))
                        {
                            auto bookBase = keylet::quality({ltDIR_NODE, k}, 0);
                            if (!inBase && inDesired)
                            {
                                auto firstBook = desired->stateMap().upperBound(bookBase.key);
                                if (firstBook != desired->stateMap().end() &&
                                    firstBook->key() < getQualityNext(bookBase.key) &&
                                    firstBook->key() == k)
                                {
                                    auto succ = response.add_book_successors();
                                    succ->set_book_base(bookBase.key.data(), bookBase.key.size());
                                    succ->set_first_book(
                                        firstBook->key().data(), firstBook->key().size());
                                }
                            }
                            if (inBase && !inDesired)
                            {
                                auto oldFirstBook = base->stateMap().upperBound(bookBase.key);
                                if (oldFirstBook != base->stateMap().end() &&
                                    oldFirstBook->key() < getQualityNext(bookBase.key) &&
                                    oldFirstBook->key() == k)
                                {
                                    auto succ = response.add_book_successors();
                                    succ->set_book_base(bookBase.key.data(), bookBase.key.size());
                                    auto newFirstBook =
                                        desired->stateMap().upperBound(bookBase.key);

                                    if (newFirstBook != desired->stateMap().end() &&
                                        newFirstBook->key() < getQualityNext(bookBase.key))
                                    {
                                        succ->set_first_book(
                                            newFirstBook->key().data(), newFirstBook->key().size());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        response.set_objects_included(true);
        response.set_object_neighbors_included(request.get_object_neighbors());
        response.set_skiplist_included(true);
    }

    response.set_validated(context.ledgerMaster.isValidated(*ledger));

    auto end = std::chrono::system_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() * 1.0;
    JLOG(context.j.warn()) << __func__ << " - Extract time = " << duration
                           << " - num objects = " << response.ledger_objects().objects_size()
                           << " - num txns = " << response.transactions_list().transactions_size()
                           << " - ms per obj "
                           << duration / response.ledger_objects().objects_size()
                           << " - ms per txn "
                           << duration / response.transactions_list().transactions_size();

    return {response, status};
}
}  // namespace xrpl
