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
    rpc::v1::XRPLedgerAPIService::AsyncService& service,
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
    , aborted_(false)
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
    app_.getJobQueue().postCoro(
        JobType::jtRPC,
        "gRPC-Client",
        [thisShared](std::shared_ptr<JobQueue::Coro> coro) {
            std::lock_guard lock{thisShared->mut_};

            // Do nothing if call has been aborted due to server shutdown
            // or if handler was already executed
            if (thisShared->aborted_ || thisShared->finished_)
                return;

            thisShared->process(coro);
            thisShared->finished_ = true;
        });
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
            grpc::Status status{grpc::StatusCode::RESOURCE_EXHAUSTED,
                                "usage balance exceeds threshhold"};
            responder_.FinishWithError(status, this);
        }
        else
        {
            auto loadType = getLoadType();
            usage.charge(loadType);
            auto role = getRole();

            RPC::GRPCContext<Request> context{{app_.journal("gRPCServer"),
                                               app_,
                                               loadType,
                                               app_.getOPs(),
                                               app_.getLedgerMaster(),
                                               usage,
                                               role,
                                               coro,
                                               InfoSub::pointer()},
                                              request_};

            // Make sure we can currently handle the rpc
            error_code_i conditionMetRes =
                RPC::conditionMet(requiredCondition_, context);

            if (conditionMetRes != rpcSUCCESS)
            {
                RPC::ErrorInfo errorInfo = RPC::get_error_info(conditionMetRes);
                grpc::Status status{grpc::StatusCode::INTERNAL,
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
    // Need to lock here because this object can be returned from cq_.Next(..)
    // as soon as the response is sent, which could be before finished_ is set
    // to true, causing the handler to be executed twice
    std::lock_guard lock{mut_};
    return finished_;
}

template <class Request, class Response>
void
GRPCServerImpl::CallData<Request, Response>::abort()
{
    std::lock_guard lock{mut_};
    aborted_ = true;
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

GRPCServerImpl::GRPCServerImpl(Application& app) : app_(app)
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
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    cq_->Shutdown();
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
    while (cq_->Next(&tag, &ok))
    {
        auto ptr = static_cast<Processor*>(tag);
        // if ok is false, event was terminated as part of a shutdown sequence
        // need to abort any further processing
        if (!ok)
        {
            // abort first, then erase. Otherwise, erase can delete object
            ptr->abort();
            erase(ptr);
        }
        else
        {
            if (!ptr->isFinished())
            {
                // ptr is now processing a request, so create a new CallData
                // object to handle additional requests
                auto cloned = ptr->clone();
                requests.push_back(cloned);
                // process the request
                ptr->process();
            }
            else
            {
                erase(ptr);
            }
        }
    }
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
        using cd = CallData<rpc::v1::GetFeeRequest, rpc::v1::GetFeeResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &rpc::v1::XRPLedgerAPIService::AsyncService::RequestGetFee,
            doFeeGrpc,
            RPC::NEEDS_CURRENT_LEDGER,
            Resource::feeReferenceRPC));
    }
    {
        using cd = CallData<
            rpc::v1::GetAccountInfoRequest,
            rpc::v1::GetAccountInfoResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &rpc::v1::XRPLedgerAPIService::AsyncService::RequestGetAccountInfo,
            doAccountInfoGrpc,
            RPC::NEEDS_CURRENT_LEDGER,
            Resource::feeReferenceRPC));
    }
    {
        using cd = CallData<rpc::v1::GetTxRequest, rpc::v1::GetTxResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &rpc::v1::XRPLedgerAPIService::AsyncService::RequestGetTx,
            doTxGrpc,
            RPC::NEEDS_CURRENT_LEDGER,
            Resource::feeReferenceRPC));
    }
    {
        using cd = CallData<
            rpc::v1::SubmitTransactionRequest,
            rpc::v1::SubmitTransactionResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestSubmitTransaction,
            doSubmitGrpc,
            RPC::NEEDS_CURRENT_LEDGER,
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
GRPCServer::run()
{
    // Start the server and setup listeners
    if ((running_ = impl_.start()))
    {
        thread_ = std::thread([this]() {
            // Start the event loop and begin handling requests
            this->impl_.handleRpcs();
        });
    }
}

GRPCServer::~GRPCServer()
{
    if (running_)
    {
        impl_.shutdown();
        thread_.join();
    }
}

}  // namespace ripple
