//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/main/GRPCServer.h>
#include <ripple/resource/Fees.h>

namespace ripple {

namespace {

// helper function. strips scheme from endpoint string
std::string
getEndpoint(std::string const& peer)
{
    std::size_t first = peer.find_first_of(":");
    std::size_t last = peer.find_last_of(":");
    std::string peerClean(peer);
    if (first != last)
    {
        peerClean = peer.substr(first + 1);
    }
    return peerClean;
}
}  // namespace

template <class Request, class Response>
GRPCServerImpl::CallData<Request, Response>::CallData(
    org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService& service,
    grpc::ServerCompletionQueue& cq,
    Application& app,
    BindListener<Request, Response> bindListener,
    Handler<Request, Response> handler,
    RPC::Condition requiredCondition,
    Resource::Charge loadType)
    : service_(service)
    , cq_(cq)
    , finished_(false)
    , app_(app)
    , responder_(&ctx_)
    , bindListener_(std::move(bindListener))
    , handler_(std::move(handler))
    , requiredCondition_(std::move(requiredCondition))
    , loadType_(std::move(loadType))
{
    // Bind a listener. When a request is received, "this" will be returned
    // from CompletionQueue::Next
    bindListener_(service_, &ctx_, &request_, &responder_, &cq_, &cq_, this);
}

template <class Request, class Response>
std::shared_ptr<Processor>
GRPCServerImpl::CallData<Request, Response>::clone()
{
    return std::make_shared<CallData<Request, Response>>(
        service_,
        cq_,
        app_,
        bindListener_,
        handler_,
        requiredCondition_,
        loadType_);
}

template <class Request, class Response>
void
GRPCServerImpl::CallData<Request, Response>::process()
{
    // sanity check
    BOOST_ASSERT(!finished_);

    std::shared_ptr<CallData<Request, Response>> thisShared =
        this->shared_from_this();

    // Need to set finished to true before processing the response,
    // because as soon as the response is posted to the completion
    // queue (via responder_.Finish(...) or responder_.FinishWithError(...)),
    // the CallData object is returned as a tag in handleRpcs().
    // handleRpcs() checks the finished variable, and if true, destroys
    // the object. Setting finished to true before calling process
    // ensures that finished is always true when this CallData object
    // is returned as a tag in handleRpcs(), after sending the response
    finished_ = true;
    auto coro = app_.getJobQueue().postCoro(
        JobType::jtRPC,
        "gRPC-Client",
        [thisShared](std::shared_ptr<JobQueue::Coro> coro) {
            thisShared->process(coro);
        });

    // If coro is null, then the JobQueue has already been shutdown
    if (!coro)
    {
        grpc::Status status{
            grpc::StatusCode::INTERNAL, "Job Queue is already stopped"};
        responder_.FinishWithError(status, this);
    }
}

template <class Request, class Response>
void
GRPCServerImpl::CallData<Request, Response>::process(
    std::shared_ptr<JobQueue::Coro> coro)
{
    try
    {
        auto usage = getUsage();
        if (usage.disconnect())
        {
            grpc::Status status{
                grpc::StatusCode::RESOURCE_EXHAUSTED,
                "usage balance exceeds threshhold"};
            responder_.FinishWithError(status, this);
        }
        else
        {
            auto loadType = getLoadType();
            usage.charge(loadType);
            auto role = getRole();

            RPC::GRPCContext<Request> context{
                {app_.journal("gRPCServer"),
                 app_,
                 loadType,
                 app_.getOPs(),
                 app_.getLedgerMaster(),
                 usage,
                 role,
                 coro,
                 InfoSub::pointer(),
                 apiVersion},
                request_};

            // Make sure we can currently handle the rpc
            error_code_i conditionMetRes =
                RPC::conditionMet(requiredCondition_, context);

            if (conditionMetRes != rpcSUCCESS)
            {
                RPC::ErrorInfo errorInfo = RPC::get_error_info(conditionMetRes);
                grpc::Status status{
                    grpc::StatusCode::FAILED_PRECONDITION,
                    errorInfo.message.c_str()};
                responder_.FinishWithError(status, this);
            }
            else
            {
                std::pair<Response, grpc::Status> result = handler_(context);
                responder_.Finish(result.first, result.second, this);
            }
        }
    }
    catch (std::exception const& ex)
    {
        grpc::Status status{grpc::StatusCode::INTERNAL, ex.what()};
        responder_.FinishWithError(status, this);
    }
}

template <class Request, class Response>
bool
GRPCServerImpl::CallData<Request, Response>::isFinished()
{
    return finished_;
}

template <class Request, class Response>
Resource::Charge
GRPCServerImpl::CallData<Request, Response>::getLoadType()
{
    return loadType_;
}

template <class Request, class Response>
Role
GRPCServerImpl::CallData<Request, Response>::getRole()
{
    return Role::USER;
}

template <class Request, class Response>
Resource::Consumer
GRPCServerImpl::CallData<Request, Response>::getUsage()
{
    std::string peer = getEndpoint(ctx_.peer());
    boost::optional<beast::IP::Endpoint> endpoint =
        beast::IP::Endpoint::from_string_checked(peer);
    return app_.getResourceManager().newInboundEndpoint(endpoint.get());
}

GRPCServerImpl::GRPCServerImpl(Application& app)
    : app_(app), journal_(app_.journal("gRPC Server"))
{
    // if present, get endpoint from config
    if (app_.config().exists("port_grpc"))
    {
        Section section = app_.config().section("port_grpc");

        std::pair<std::string, bool> ipPair = section.find("ip");
        if (!ipPair.second)
            return;

        std::pair<std::string, bool> portPair = section.find("port");
        if (!portPair.second)
            return;
        try
        {
            beast::IP::Endpoint endpoint(
                boost::asio::ip::make_address(ipPair.first),
                std::stoi(portPair.first));

            serverAddress_ = endpoint.to_string();
        }
        catch (std::exception const&)
        {
        }
    }
}

void
GRPCServerImpl::shutdown()
{
    JLOG(journal_.debug()) << "Shutting down";

    // The below call cancels all "listeners" (CallData objects that are waiting
    // for a request, as opposed to processing a request), and blocks until all
    // requests being processed are completed. CallData objects in the midst of
    // processing requests need to actually send data back to the client, via
    // responder_.Finish(...) or responder_.FinishWithError(...), for this call
    // to unblock. Each cancelled listener is returned via cq_.Next(...) with ok
    // set to false
    server_->Shutdown();
    JLOG(journal_.debug()) << "Server has been shutdown";

    // Always shutdown the completion queue after the server. This call allows
    // cq_.Next() to return false, once all events posted to the completion
    // queue have been processed. See handleRpcs() for more details.
    cq_->Shutdown();
    JLOG(journal_.debug()) << "Completion Queue has been shutdown";
}

void
GRPCServerImpl::handleRpcs()
{
    // This collection should really be an unordered_set. However, to delete
    // from the unordered_set, we need a shared_ptr, but cq_.Next() (see below
    // while loop) sets the tag to a raw pointer.
    std::vector<std::shared_ptr<Processor>> requests = setupListeners();

    auto erase = [&requests](Processor* ptr) {
        auto it = std::find_if(
            requests.begin(),
            requests.end(),
            [ptr](std::shared_ptr<Processor>& sPtr) {
                return sPtr.get() == ptr;
            });
        BOOST_ASSERT(it != requests.end());
        it->swap(requests.back());
        requests.pop_back();
    };

    void* tag;  // uniquely identifies a request.
    bool ok;
    // Block waiting to read the next event from the completion queue. The
    // event is uniquely identified by its tag, which in this case is the
    // memory address of a CallData instance.
    // The return value of Next should always be checked. This return value
    // tells us whether there is any kind of event or cq_ is shutting down.
    // When cq_.Next(...) returns false, all work has been completed and the
    // loop can exit. When the server is shutdown, each CallData object that is
    // listening for a request is forceably cancelled, and is returned by
    // cq_->Next() with ok set to false. Then, each CallData object processing
    // a request must complete (by sending data to the client), each of which
    // will be returned from cq_->Next() with ok set to true. After all
    // cancelled listeners and all CallData objects processing requests are
    // returned via cq_->Next(), cq_->Next() will return false, causing the
    // loop to exit.
    while (cq_->Next(&tag, &ok))
    {
        auto ptr = static_cast<Processor*>(tag);
        JLOG(journal_.trace()) << "Processing CallData object."
                               << " ptr = " << ptr << " ok = " << ok;

        if (!ok)
        {
            JLOG(journal_.debug()) << "Request listener cancelled. "
                                   << "Destroying object";
            erase(ptr);
        }
        else
        {
            if (!ptr->isFinished())
            {
                JLOG(journal_.debug()) << "Received new request. Processing";
                // ptr is now processing a request, so create a new CallData
                // object to handle additional requests
                auto cloned = ptr->clone();
                requests.push_back(cloned);
                // process the request
                ptr->process();
            }
            else
            {
                JLOG(journal_.debug()) << "Sent response. Destroying object";
                erase(ptr);
            }
        }
    }
    JLOG(journal_.debug()) << "Completion Queue drained";
}

// create a CallData instance for each RPC
std::vector<std::shared_ptr<Processor>>
GRPCServerImpl::setupListeners()
{
    std::vector<std::shared_ptr<Processor>> requests;

    auto addToRequests = [&requests](auto callData) {
        requests.push_back(std::move(callData));
    };

    {
        using cd = CallData<
            org::xrpl::rpc::v1::GetFeeRequest,
            org::xrpl::rpc::v1::GetFeeResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestGetFee,
            doFeeGrpc,
            RPC::NEEDS_CURRENT_LEDGER,
            Resource::feeReferenceRPC));
    }
    {
        using cd = CallData<
            org::xrpl::rpc::v1::GetAccountInfoRequest,
            org::xrpl::rpc::v1::GetAccountInfoResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestGetAccountInfo,
            doAccountInfoGrpc,
            RPC::NO_CONDITION,
            Resource::feeReferenceRPC));
    }
    {
        using cd = CallData<
            org::xrpl::rpc::v1::GetTransactionRequest,
            org::xrpl::rpc::v1::GetTransactionResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestGetTransaction,
            doTxGrpc,
            RPC::NEEDS_CURRENT_LEDGER,
            Resource::feeReferenceRPC));
    }
    {
        using cd = CallData<
            org::xrpl::rpc::v1::SubmitTransactionRequest,
            org::xrpl::rpc::v1::SubmitTransactionResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestSubmitTransaction,
            doSubmitGrpc,
            RPC::NEEDS_CURRENT_LEDGER,
            Resource::feeMediumBurdenRPC));
    }

    {
        using cd = CallData<
            org::xrpl::rpc::v1::GetAccountTransactionHistoryRequest,
            org::xrpl::rpc::v1::GetAccountTransactionHistoryResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestGetAccountTransactionHistory,
            doAccountTxGrpc,
            RPC::NO_CONDITION,
            Resource::feeMediumBurdenRPC));
    }
    return requests;
};

bool
GRPCServerImpl::start()
{
    // if config does not specify a grpc server address, don't start
    if (serverAddress_.empty())
        return false;

    JLOG(journal_.info()) << "Starting gRPC server at " << serverAddress_;

    grpc::ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(serverAddress_, grpc::InsecureServerCredentials());
    // Register "service_" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *asynchronous* service.
    builder.RegisterService(&service_);
    // Get hold of the completion queue used for the asynchronous communication
    // with the gRPC runtime.
    cq_ = builder.AddCompletionQueue();
    // Finally assemble the server.
    server_ = builder.BuildAndStart();

    return true;
}

void
GRPCServer::onStart()
{
    // Start the server and setup listeners
    if (running_ = impl_.start(); running_)
    {
        thread_ = std::thread([this]() {
            // Start the event loop and begin handling requests
            beast::setCurrentThreadName("rippled: grpc");
            this->impl_.handleRpcs();
        });
    }
}

void
GRPCServer::onStop()
{
    if (running_)
    {
        impl_.shutdown();
        thread_.join();
        running_ = false;
    }

    stopped();
}

GRPCServer::~GRPCServer()
{
    assert(!running_);
}

}  // namespace ripple
