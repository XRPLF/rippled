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
#include <ripple/app/reporting/P2pProxy.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/resource/Fees.h>

#include <beast/net/IPAddressConversion.h>

namespace ripple {

namespace {

// helper function. converts string to endpoint. handles ipv4 and ipv6, with or
// without port, with or without prepended scheme
std::optional<boost::asio::ip::tcp::endpoint>
getEndpoint(std::string const& peer)
{
    try
    {
        std::size_t first = peer.find_first_of(":");
        std::size_t last = peer.find_last_of(":");
        std::string peerClean(peer);
        if (first != last)
        {
            peerClean = peer.substr(first + 1);
        }

        boost::optional<beast::IP::Endpoint> endpoint =
            beast::IP::Endpoint::from_string_checked(peerClean);
        if (endpoint)
            return beast::IP::to_asio_endpoint(endpoint.value());
    }
    catch (std::exception const&)
    {
    }
    return {};
}

}  // namespace

template <class Request, class Response>
GRPCServerImpl::CallData<Request, Response>::CallData(
    org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService& service,
    grpc::ServerCompletionQueue& cq,
    Application& app,
    BindListener<Request, Response> bindListener,
    Handler<Request, Response> handler,
    Forward<Request, Response> forward,
    RPC::Condition requiredCondition,
    Resource::Charge loadType,
    std::vector<boost::asio::ip::address> const& secureGatewayIPs)
    : service_(service)
    , cq_(cq)
    , finished_(false)
    , app_(app)
    , responder_(&ctx_)
    , bindListener_(std::move(bindListener))
    , handler_(std::move(handler))
    , forward_(std::move(forward))
    , requiredCondition_(std::move(requiredCondition))
    , loadType_(std::move(loadType))
    , secureGatewayIPs_(secureGatewayIPs)
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
        forward_,
        requiredCondition_,
        loadType_,
        secureGatewayIPs_);
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
        bool isUnlimited = clientIsUnlimited();
        if (!isUnlimited && usage.disconnect())
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
            auto role = getRole(isUnlimited);

            {
                std::stringstream toLog;
                toLog << "role = " << (int)role;

                toLog << " address = ";
                if (auto clientIp = getClientIpAddress())
                    toLog << clientIp.value();

                toLog << " user = ";
                if (auto user = getUser())
                    toLog << user.value();
                toLog << " isUnlimited = " << isUnlimited;

                JLOG(app_.journal("GRPCServer::Calldata").debug())
                    << toLog.str();
            }

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
            if (shouldForwardToP2p(context, requiredCondition_))
            {
                forwardToP2p(context);
                return;
            }

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
                try
                {
                    std::pair<Response, grpc::Status> result =
                        handler_(context);
                    setIsUnlimited(result.first, isUnlimited);
                    responder_.Finish(result.first, result.second, this);
                }
                catch (ReportingShouldProxy&)
                {
                    forwardToP2p(context);
                    return;
                }
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
void
GRPCServerImpl::CallData<Request, Response>::forwardToP2p(
    RPC::GRPCContext<Request>& context)
{
    if (auto descriptor =
            Request::GetDescriptor()->FindFieldByName("client_ip"))
    {
        Request::GetReflection()->SetString(&request_, descriptor, ctx_.peer());
        JLOG(app_.journal("gRPCServer").debug())
            << "Set client_ip to " << ctx_.peer();
    }
    else
    {
        assert(false);
        Throw<std::runtime_error>(
            "Attempting to forward but no client_ip field in "
            "protobuf message");
    }
    auto stub = getP2pForwardingStub(context);
    if (stub)
    {
        grpc::ClientContext clientContext;
        Response response;
        auto status = forward_(stub.get(), &clientContext, request_, &response);
        responder_.Finish(response, status, this);
        JLOG(app_.journal("gRPCServer").debug()) << "Forwarded request to tx";
    }
    else
    {
        JLOG(app_.journal("gRPCServer").error())
            << "Failed to forward request to tx";
        grpc::Status status{
            grpc::StatusCode::INTERNAL,
            "Attempted to act as proxy but failed "
            "to create forwarding stub"};
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
GRPCServerImpl::CallData<Request, Response>::getRole(bool isUnlimited)
{
    if (isUnlimited)
        return Role::IDENTIFIED;
    else if (wasForwarded())
        return Role::PROXY;
    else
        return Role::USER;
}

template <class Request, class Response>
bool
GRPCServerImpl::CallData<Request, Response>::wasForwarded()
{
    if (auto descriptor =
            Request::GetDescriptor()->FindFieldByName("client_ip"))
    {
        std::string clientIp =
            Request::GetReflection()->GetString(request_, descriptor);
        if (!clientIp.empty())
        {
            return true;
        }
    }
    return false;
}

template <class Request, class Response>
std::optional<std::string>
GRPCServerImpl::CallData<Request, Response>::getUser()
{
    if (auto descriptor = Request::GetDescriptor()->FindFieldByName("user"))
    {
        std::string user =
            Request::GetReflection()->GetString(request_, descriptor);
        if (!user.empty())
        {
            return user;
        }
    }
    return {};
}

template <class Request, class Response>
std::optional<boost::asio::ip::address>
GRPCServerImpl::CallData<Request, Response>::getClientIpAddress()
{
    auto endpoint = getClientEndpoint();
    if (endpoint)
        return endpoint->address();
    return {};
}

template <class Request, class Response>
std::optional<boost::asio::ip::address>
GRPCServerImpl::CallData<Request, Response>::getProxiedClientIpAddress()
{
    auto endpoint = getProxiedClientEndpoint();
    if (endpoint)
        return endpoint->address();
    return {};
}

template <class Request, class Response>
std::optional<boost::asio::ip::tcp::endpoint>
GRPCServerImpl::CallData<Request, Response>::getProxiedClientEndpoint()
{
    auto descriptor = Request::GetDescriptor()->FindFieldByName("client_ip");
    if (descriptor)
    {
        std::string clientIp =
            Request::GetReflection()->GetString(request_, descriptor);
        if (!clientIp.empty())
        {
            JLOG(app_.journal("gRPCServer").debug())
                << "Got client_ip from request : " << clientIp;
            return getEndpoint(clientIp);
        }
    }
    return {};
}

template <class Request, class Response>
std::optional<boost::asio::ip::tcp::endpoint>
GRPCServerImpl::CallData<Request, Response>::getClientEndpoint()
{
    return getEndpoint(ctx_.peer());
}

template <class Request, class Response>
bool
GRPCServerImpl::CallData<Request, Response>::clientIsUnlimited()
{
    if (!getUser())
        return false;
    auto clientIp = getClientIpAddress();
    auto proxiedIp = getProxiedClientIpAddress();
    if (clientIp && !proxiedIp)
    {
        for (auto& ip : secureGatewayIPs_)
        {
            if (ip == clientIp)
                return true;
        }
    }
    return false;
}

template <class Request, class Response>
void
GRPCServerImpl::CallData<Request, Response>::setIsUnlimited(
    Response& response,
    bool isUnlimited)
{
    if (isUnlimited)
    {
        if (auto descriptor =
                Response::GetDescriptor()->FindFieldByName("is_unlimited"))
        {
            Response::GetReflection()->SetBool(&response, descriptor, true);
        }
    }
}

template <class Request, class Response>
Resource::Consumer
GRPCServerImpl::CallData<Request, Response>::getUsage()
{
    auto endpoint = getClientEndpoint();
    auto proxiedEndpoint = getProxiedClientEndpoint();
    if (proxiedEndpoint)
        return app_.getResourceManager().newInboundEndpoint(
            beast::IP::from_asio(proxiedEndpoint.value()));
    else if (endpoint)
        return app_.getResourceManager().newInboundEndpoint(
            beast::IP::from_asio(endpoint.value()));
    Throw<std::runtime_error>("Failed to get client endpoint");
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
            boost::asio::ip::tcp::endpoint endpoint(
                boost::asio::ip::make_address(ipPair.first),
                std::stoi(portPair.first));

            std::stringstream ss;
            ss << endpoint;
            serverAddress_ = ss.str();
        }
        catch (std::exception const&)
        {
            JLOG(journal_.error()) << "Error setting grpc server address";
            Throw<std::exception>();
        }

        std::pair<std::string, bool> secureGateway =
            section.find("secure_gateway");
        if (secureGateway.second)
        {
            try
            {
                std::stringstream ss{secureGateway.first};
                std::string ip;
                while (std::getline(ss, ip, ','))
                {
                    boost::algorithm::trim(ip);
                    auto const addr = boost::asio::ip::make_address(ip);

                    if (addr.is_unspecified())
                    {
                        JLOG(journal_.error())
                            << "Can't pass unspecified IP in "
                            << "secure_gateway section of port_grpc";
                        Throw<std::exception>();
                    }

                    secureGatewayIPs_.emplace_back(addr);
                }
            }
            catch (std::exception const&)
            {
                JLOG(journal_.error())
                    << "Error parsing secure gateway IPs for grpc server";
                Throw<std::exception>();
            }
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
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::GetFee,
            RPC::NEEDS_CURRENT_LEDGER,
            Resource::feeReferenceRPC,
            secureGatewayIPs_));
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
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::GetAccountInfo,
            RPC::NO_CONDITION,
            Resource::feeReferenceRPC,
            secureGatewayIPs_));
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
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::GetTransaction,
            RPC::NEEDS_NETWORK_CONNECTION,
            Resource::feeReferenceRPC,
            secureGatewayIPs_));
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
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::SubmitTransaction,
            RPC::NEEDS_CURRENT_LEDGER,
            Resource::feeMediumBurdenRPC,
            secureGatewayIPs_));
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
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::
                GetAccountTransactionHistory,
            RPC::NO_CONDITION,
            Resource::feeMediumBurdenRPC,
            secureGatewayIPs_));
    }

    {
        using cd = CallData<
            org::xrpl::rpc::v1::GetLedgerRequest,
            org::xrpl::rpc::v1::GetLedgerResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestGetLedger,
            doLedgerGrpc,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::GetLedger,
            RPC::NO_CONDITION,
            Resource::feeMediumBurdenRPC,
            secureGatewayIPs_));
    }
    {
        using cd = CallData<
            org::xrpl::rpc::v1::GetLedgerDataRequest,
            org::xrpl::rpc::v1::GetLedgerDataResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestGetLedgerData,
            doLedgerDataGrpc,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::GetLedgerData,
            RPC::NO_CONDITION,
            Resource::feeMediumBurdenRPC,
            secureGatewayIPs_));
    }
    {
        using cd = CallData<
            org::xrpl::rpc::v1::GetLedgerDiffRequest,
            org::xrpl::rpc::v1::GetLedgerDiffResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestGetLedgerDiff,
            doLedgerDiffGrpc,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::GetLedgerDiff,
            RPC::NO_CONDITION,
            Resource::feeMediumBurdenRPC,
            secureGatewayIPs_));
    }
    {
        using cd = CallData<
            org::xrpl::rpc::v1::GetLedgerEntryRequest,
            org::xrpl::rpc::v1::GetLedgerEntryResponse>;

        addToRequests(std::make_shared<cd>(
            service_,
            *cq_,
            app_,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::
                RequestGetLedgerEntry,
            doLedgerEntryGrpc,
            &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::GetLedgerEntry,
            RPC::NO_CONDITION,
            Resource::feeMediumBurdenRPC,
            secureGatewayIPs_));
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
            beast::setCurrentThreadName("rippled : GRPCServer");
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
}

GRPCServer::~GRPCServer()
{
    assert(!running_);
}

}  // namespace ripple
