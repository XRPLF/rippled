#include <xrpld/rpc/handlers/ledger/Ledger.h>

#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
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
#include <rpcspec/Errors.hpp>

#include <chrono>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace xrpl {
namespace rpc {

LedgerHandler::LedgerHandler(JsonContext& context) : context_(context)
{
}

std::expected<LedgerHandler::Output, ::rpc::Status>
LedgerHandler::process(Input const& input) const
{
    Output output;
    output.options = (input.full ? static_cast<int>(LedgerFill::Options::Full) : 0) |
        (input.expand ? static_cast<int>(LedgerFill::Options::Expand) : 0) |
        (input.transactions ? static_cast<int>(LedgerFill::Options::DumpTxrp) : 0) |
        (input.accounts ? static_cast<int>(LedgerFill::Options::DumpState) : 0) |
        (input.binary ? static_cast<int>(LedgerFill::Options::Binary) : 0) |
        (input.ownerFunds ? static_cast<int>(LedgerFill::Options::OwnerFunds) : 0) |
        (input.queue ? static_cast<int>(LedgerFill::Options::DumpQueue) : 0);

    if (input.ledger.isUnspecified())
        return output;

    if (auto const status = getLedger(output.ledger, input.ledger, context_.get()))
        return std::unexpected{::rpc::Status{status.toErrorCode(), status.message()}};

    if (input.full || input.accounts)
    {
        // Until some sane way to get full ledgers has been implemented,
        // disallow retrieving all state nodes.
        if (!isUnlimited(context_.get().role))
            return std::unexpected{::rpc::Status{RpcNoPermission}};

        // Dead code: the check above already returns for any role that is not
        // unlimited, so this condition can never be true. Safe to remove; kept
        // to keep this migration a strict port of the original handler.
        if (context_.get().app.getFeeTrack().isLoadedLocal() && !isUnlimited(context_.get().role))
        {
            return std::unexpected{::rpc::Status{RpcTooBusy}};
        }
        context_.get().loadType =
            input.binary ? resource::kFeeMediumBurdenRpc : resource::kFeeHeavyBurdenRpc;
    }

    if (input.queue)
    {
        if (!output.ledger || !output.ledger->open())
        {
            // It doesn't make sense to request the queue
            // with a non-existent or closed/validated ledger.
            return std::unexpected{::rpc::Status{RpcInvalidParams}};
        }

        output.queueTxs = context_.get().app.getTxQ().getTxs();
    }

    return output;
}

void
LedgerHandler::writeResult(json::Value& value, Output const& output) const
{
    if (output.ledger)
    {
        auto const& header = output.ledger->header();

        if (output.ledger->open())
        {
            value[jss::ledger_current_index] = header.seq;
        }
        else
        {
            value[jss::ledger_hash] = to_string(header.hash);
            value[jss::ledger_index] = header.seq;
        }

        value[jss::validated] = context_.get().ledgerMaster.isValidated(*output.ledger);

        addJson(value, {*output.ledger, &context_.get(), output.options, output.queueTxs});
    }
    else
    {
        auto& master = context_.get().app.getLedgerMaster();
        {
            auto& closed = value[jss::closed] = json::ValueType::Object;
            addJson(closed, {*master.getClosedLedger(), &context_.get(), 0});
        }
        {
            auto& open = value[jss::open] = json::ValueType::Object;
            addJson(open, {*master.getCurrentLedger(), &context_.get(), 0});
        }
    }
}

}  // namespace rpc

std::pair<org::xrpl::rpc::v1::GetLedgerResponse, grpc::Status>
doLedgerGrpc(rpc::GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest>& context)
{
    auto begin = std::chrono::system_clock::now();
    org::xrpl::rpc::v1::GetLedgerRequest const& request = context.params;
    org::xrpl::rpc::v1::GetLedgerResponse response;
    grpc::Status const status = grpc::Status::OK;

    std::shared_ptr<ReadView const> ledger;
    if (auto status = rpc::ledgerFromRequest(ledger, context))
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
    // Guard the per-item rates: an empty ledger has zero objects and/or zero
    // transactions, and dividing by zero is undefined for these doubles.
    auto const numObjects = response.ledger_objects().objects_size();
    auto const numTxns = response.transactions_list().transactions_size();
    std::string const msPerObj = numObjects > 0 ? std::to_string(duration / numObjects) : "n/a";
    std::string const msPerTxn = numTxns > 0 ? std::to_string(duration / numTxns) : "n/a";
    JLOG(context.j.warn()) << __func__ << " - Extract time = " << duration
                           << " - num objects = " << numObjects << " - num txns = " << numTxns
                           << " - ms per obj " << msPerObj << " - ms per txn " << msPerTxn;

    return {response, status};
}
}  // namespace xrpl
