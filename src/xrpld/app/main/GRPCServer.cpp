#include <xrpld/app/main/GRPCServer.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/InfoSub.h>

#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/icl/interval_set.hpp>

#include <grpc/grpc_security_constants.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/status.h>
#include <org/xrpl/rpc/v1/get_ledger.pb.h>
#include <org/xrpl/rpc/v1/get_ledger_data.pb.h>
#include <org/xrpl/rpc/v1/get_ledger_diff.pb.h>
#include <org/xrpl/rpc/v1/get_ledger_entry.pb.h>
#include <org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// helper function. converts string to endpoint. handles ipv4 and ipv6, with or
// without port, with or without prepended scheme
std::optional<boost::asio::ip::tcp::endpoint>
getEndpoint(std::string const& peer)
{
    try
    {
        std::size_t const first = peer.find_first_of(':');
        std::size_t const last = peer.find_last_of(':');
        std::string peerClean(peer);
        if (first != last)
        {
            peerClean = peer.substr(first + 1);
        }

        std::optional<beast::IP::Endpoint> endpoint =
            beast::IP::Endpoint::from_string_checked(peerClean);
        if (endpoint)
            return beast::IP::to_asio_endpoint(endpoint.value());
    }
    catch (std::exception const&)  // NOLINT(bugprone-empty-catch)
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
    , requiredCondition_(requiredCondition)
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

    std::shared_ptr<CallData<Request, Response>> const thisShared = this->shared_from_this();

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
        JobType::jtRPC, "gRPC-Client", [thisShared](std::shared_ptr<JobQueue::Coro> coro) {
            thisShared->process(coro);
        });

    // If coro is null, then the JobQueue has already been shutdown
    if (!coro)
    {
        grpc::Status const status{grpc::StatusCode::INTERNAL, "Job Queue is already stopped"};
        responder_.FinishWithError(status, this);
    }
}

template <class Request, class Response>
void
GRPCServerImpl::CallData<Request, Response>::process(std::shared_ptr<JobQueue::Coro> coro)
{
    try
    {
        auto usage = getUsage();
        bool const isUnlimited = clientIsUnlimited();
        if (!isUnlimited && usage.disconnect(app_.getJournal("gRPCServer")))
        {
            grpc::Status const status{
                grpc::StatusCode::RESOURCE_EXHAUSTED, "usage balance exceeds threshold"};
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

                JLOG(app_.getJournal("GRPCServer::Calldata").debug()) << toLog.str();
            }

            RPC::GRPCContext<Request> context{
                {app_.getJournal("gRPCServer"),
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
            error_code_i const conditionMetRes = RPC::conditionMet(requiredCondition_, context);

            if (conditionMetRes != rpcSUCCESS)
            {
                RPC::ErrorInfo const errorInfo = RPC::get_error_info(conditionMetRes);
                grpc::Status const status{
                    grpc::StatusCode::FAILED_PRECONDITION, errorInfo.message.c_str()};
                responder_.FinishWithError(status, this);
            }
            else
            {
                std::pair<Response, grpc::Status> result = handler_(context);
                setIsUnlimited(result.first, isUnlimited);
                responder_.Finish(result.first, result.second, this);
            }
        }
    }
    catch (std::exception const& ex)
    {
        grpc::Status const status{grpc::StatusCode::INTERNAL, ex.what()};
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
    {
        return Role::IDENTIFIED;
    }

    return Role::USER;
}

template <class Request, class Response>
std::optional<std::string>
GRPCServerImpl::CallData<Request, Response>::getUser()
{
    if (auto descriptor = Request::GetDescriptor()->FindFieldByName("user"))
    {
        std::string user = Request::GetReflection()->GetString(request_, descriptor);
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
std::optional<boost::asio::ip::tcp::endpoint>
GRPCServerImpl::CallData<Request, Response>::getClientEndpoint()
{
    return xrpl::getEndpoint(ctx_.peer());
}

template <class Request, class Response>
bool
GRPCServerImpl::CallData<Request, Response>::clientIsUnlimited()
{
    if (!getUser())
        return false;
    auto clientIp = getClientIpAddress();
    if (clientIp)
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
GRPCServerImpl::CallData<Request, Response>::setIsUnlimited(Response& response, bool isUnlimited)
{
    if (isUnlimited)
    {
        if (auto descriptor = Response::GetDescriptor()->FindFieldByName("is_unlimited"))
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
    if (endpoint)
        return app_.getResourceManager().newInboundEndpoint(beast::IP::from_asio(endpoint.value()));
    Throw<std::runtime_error>("Failed to get client endpoint");
}

GRPCServerImpl::GRPCServerImpl(Application& app)
    : app_(app), journal_(app_.getJournal("gRPC Server"))
{
    // if present, get endpoint from config
    if (app_.config().exists(SECTION_PORT_GRPC))
    {
        Section const& section = app_.config().section(SECTION_PORT_GRPC);

        auto const optIp = section.get("ip");
        if (!optIp)
            return;

        auto const optPort = section.get("port");
        if (!optPort)
            return;
        try
        {
            boost::asio::ip::tcp::endpoint const endpoint(
                boost::asio::ip::make_address(*optIp), std::stoi(*optPort));

            std::stringstream ss;
            ss << endpoint;
            serverAddress_ = ss.str();
        }
        catch (std::exception const&)
        {
            JLOG(journal_.error()) << "Error setting grpc server address";
            Throw<std::runtime_error>("Error setting grpc server address");
        }

        auto const optSecureGateway = section.get("secure_gateway");
        if (optSecureGateway)
        {
            try
            {
                std::stringstream ss{*optSecureGateway};
                std::string ip;
                while (std::getline(ss, ip, ','))
                {
                    boost::algorithm::trim(ip);
                    auto const addr = boost::asio::ip::make_address(ip);

                    if (addr.is_unspecified())
                    {
                        JLOG(journal_.error()) << "Can't pass unspecified IP in "
                                               << "secure_gateway section of port_grpc";
                        Throw<std::runtime_error>("Unspecified IP in secure_gateway section");
                    }

                    secureGatewayIPs_.emplace_back(addr);
                }
            }
            catch (std::exception const&)
            {
                JLOG(journal_.error()) << "Error parsing secure gateway IPs for grpc server";
                Throw<std::runtime_error>("Error parsing secure_gateway section");
            }
        }

        // Read TLS certificate configuration (optional)
        sslCertPath_ = section.get("ssl_cert");
        sslKeyPath_ = section.get("ssl_key");
        sslCertChainPath_ = section.get("ssl_cert_chain");
        sslClientCAPath_ = section.get("ssl_client_ca");

        // If cert or key is specified, both must be specified
        if (sslCertPath_.has_value() || sslKeyPath_.has_value())
        {
            if (!sslCertPath_.has_value() || !sslKeyPath_.has_value())
            {
                JLOG(journal_.error())
                    << "Both ssl_cert and ssl_key must be specified for gRPC TLS";
                Throw<std::runtime_error>("Incomplete TLS configuration for gRPC");
            }
            JLOG(journal_.info()) << "gRPC TLS enabled with certificate: " << *sslCertPath_;
        }

        // Validate TLS configuration consistency: ssl_cert_chain only makes sense when TLS is
        // enabled
        if (sslCertChainPath_.has_value() &&
            (!sslCertPath_.has_value() || !sslKeyPath_.has_value()))
        {
            JLOG(journal_.error())
                << "ssl_cert_chain specified for gRPC without both ssl_cert and ssl_key; "
                << "this is an invalid TLS configuration";
            Throw<std::runtime_error>(
                "Invalid gRPC TLS configuration: ssl_cert_chain requires both ssl_cert and "
                "ssl_key");
        }

        // Validate TLS configuration consistency: ssl_client_ca only makes sense when TLS is
        // enabled
        if (sslClientCAPath_.has_value() && (!sslCertPath_.has_value() || !sslKeyPath_.has_value()))
        {
            JLOG(journal_.error())
                << "ssl_client_ca specified for gRPC without both ssl_cert and ssl_key; "
                << "this is an invalid TLS configuration";
            Throw<std::runtime_error>(
                "Invalid gRPC TLS configuration: ssl_client_ca requires both ssl_cert and ssl_key");
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
        auto it = std::ranges::find_if(
            requests, [ptr](std::shared_ptr<Processor>& sPtr) { return sPtr.get() == ptr; });
        BOOST_ASSERT(it != requests.end());
        it->swap(requests.back());
        requests.pop_back();
    };

    void* tag = nullptr;  // uniquely identifies a request.
    bool ok = false;
    // Block waiting to read the next event from the completion queue. The
    // event is uniquely identified by its tag, which in this case is the
    // memory address of a CallData instance.
    // The return value of Next should always be checked. This return value
    // tells us whether there is any kind of event or cq_ is shutting down.
    // When cq_.Next(...) returns false, all work has been completed and the
    // loop can exit. When the server is shutdown, each CallData object that is
    // listening for a request is forcibly cancelled, and is returned by
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

    auto addToRequests = [&requests](auto callData) { requests.push_back(std::move(callData)); };

    {
        using cd =
            CallData<org::xrpl::rpc::v1::GetLedgerRequest, org::xrpl::rpc::v1::GetLedgerResponse>;

        addToRequests(
            std::make_shared<cd>(
                service_,
                *cq_,
                app_,
                &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::RequestGetLedger,
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

        addToRequests(
            std::make_shared<cd>(
                service_,
                *cq_,
                app_,
                &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::RequestGetLedgerData,
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

        addToRequests(
            std::make_shared<cd>(
                service_,
                *cq_,
                app_,
                &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::RequestGetLedgerDiff,
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

        addToRequests(
            std::make_shared<cd>(
                service_,
                *cq_,
                app_,
                &org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::RequestGetLedgerEntry,
                doLedgerEntryGrpc,
                &org::xrpl::rpc::v1::XRPLedgerAPIService::Stub::GetLedgerEntry,
                RPC::NO_CONDITION,
                Resource::feeMediumBurdenRPC,
                secureGatewayIPs_));
    }
    return requests;
}

std::shared_ptr<grpc::ServerCredentials>
GRPCServerImpl::createServerCredentials()
{
    if (not sslCertPath_.has_value() or not sslKeyPath_.has_value())
    {
        JLOG(journal_.info()) << "Configuring gRPC server without TLS";
        return grpc::InsecureServerCredentials();
    }

    JLOG(journal_.info()) << "Configuring gRPC server with TLS";

    try
    {
        boost::system::error_code ec;
        grpc::SslServerCredentialsOptions sslOpts;
        grpc::SslServerCredentialsOptions::PemKeyCertPair keyCertPair;

        std::string const certContents = getFileContents(ec, *sslCertPath_);
        if (ec)
        {
            JLOG(journal_.error()) << "Failed to read gRPC SSL certificate file: " << *sslCertPath_
                                   << " - " << ec.message();  // LCOV_EXCL_LINE
            return nullptr;
        }

        std::string const keyContents = getFileContents(ec, *sslKeyPath_);
        if (ec)
        {
            JLOG(journal_.error()) << "Failed to read gRPC SSL key file: " << *sslKeyPath_ << " - "
                                   << ec.message();  // LCOV_EXCL_LINE
            return nullptr;
        }

        keyCertPair.private_key = keyContents;

        // Read intermediate CA certificates for server certificate chain (optional)
        std::string certChainContents;
        if (sslCertChainPath_.has_value())
        {
            certChainContents = getFileContents(ec, *sslCertChainPath_);
            if (ec)
            {
                JLOG(journal_.error())
                    << "Failed to read gRPC SSL cert chain file: " << *sslCertChainPath_ << " - "
                    << ec.message();  // LCOV_EXCL_LINE
                return nullptr;
            }
        }

        // Read CA certificate for client verification (mTLS, optional)
        if (sslClientCAPath_.has_value())
        {
            auto const clientCAContents = getFileContents(ec, *sslClientCAPath_);
            if (ec)
            {
                JLOG(journal_.error())
                    << "Failed to read gRPC SSL client CA file: " << *sslClientCAPath_ << " - "
                    << ec.message();  // LCOV_EXCL_LINE
                return nullptr;
            }

            if (clientCAContents.empty())
            {
                JLOG(journal_.error())
                    << "Empty/truncated gRPC SSL client CA file: " << *sslClientCAPath_
                    << " - failed to configure mutual TLS";  // LCOV_EXCL_LINE
                return nullptr;
            }

            sslOpts.pem_root_certs = clientCAContents;
            sslOpts.client_certificate_request =
                GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
            JLOG(journal_.info()) << "gRPC mutual TLS enabled - client certificates will be "
                                     "required and verified";
        }

        // Combine server cert with intermediate CA certs for complete chain
        keyCertPair.cert_chain = certContents;
        if (!certChainContents.empty())
        {
            keyCertPair.cert_chain += '\n' + certChainContents;
            JLOG(journal_.info()) << "gRPC server certificate chain configured with "
                                     "intermediate CA certificates";  // LCOV_EXCL_LINE
        }

        sslOpts.pem_key_cert_pairs.push_back(keyCertPair);

        JLOG(journal_.info()) << "gRPC TLS credentials configured successfully";  // LCOV_EXCL_LINE
        return grpc::SslServerCredentials(sslOpts);
    }
    catch (std::exception const& e)
    {
        JLOG(journal_.error()) << "Exception while configuring gRPC TLS: "
                               << e.what();  // LCOV_EXCL_LINE
        return nullptr;
    }
}

bool
GRPCServerImpl::start()
{
    // if config does not specify a grpc server address, don't start
    if (serverAddress_.empty())
        return false;

    // Determine TLS mode for logging
    bool const tlsEnabled = sslCertPath_.has_value() && sslKeyPath_.has_value();
    bool const mtlsEnabled = tlsEnabled && sslClientCAPath_.has_value();

    std::string tlsMode = "without TLS";
    if (mtlsEnabled)
    {
        tlsMode = "with mutual TLS (mTLS)";
    }
    else if (tlsEnabled)
    {
        tlsMode = "with TLS";
    }

    JLOG(journal_.info()) << "Starting gRPC server at " << serverAddress_ << " "
                          << tlsMode;  // LCOV_EXCL_LINE

    grpc::ServerBuilder builder;
    int port = 0;

    // Create credentials (TLS or insecure) based on configuration
    auto credentials = createServerCredentials();
    if (!credentials)
    {
        JLOG(journal_.error()) << "Failed to create gRPC server credentials for " << serverAddress_
                               << " (TLS mode: " << tlsMode
                               << ") - server will not start";  // LCOV_EXCL_LINE
        return false;
    }

    // Add listening port with appropriate credentials
    builder.AddListeningPort(serverAddress_, credentials, &port);

    // Register "service_" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *asynchronous* service.
    builder.RegisterService(&service_);

    // Get hold of the completion queue used for the asynchronous communication
    // with the gRPC runtime.
    cq_ = builder.AddCompletionQueue();

    // Finally assemble the server.
    server_ = builder.BuildAndStart();
    serverPort_ = static_cast<std::uint16_t>(port);

    if (serverPort_ != 0u)
    {
        JLOG(journal_.info()) << "gRPC server started successfully on port " << serverPort_;
    }
    else
    {
        JLOG(journal_.error())
            << "Failed to start gRPC server at " << serverAddress_ << " (TLS mode: " << tlsMode
            << "); Possible causes: address already in use, invalid address format, or permission "
               "denied";  // LCOV_EXCL_LINE
    }

    return static_cast<bool>(serverPort_);
}

boost::asio::ip::tcp::endpoint
GRPCServerImpl::getEndpoint() const
{
    std::string const addr = serverAddress_.substr(0, serverAddress_.find_last_of(':'));
    return boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(addr), serverPort_);
}

bool
GRPCServer::start()
{
    // Start the server and setup listeners
    if (running_ = impl_.start(); running_)
    {
        thread_ = std::thread([this]() {
            // Start the event loop and begin handling requests
            beast::setCurrentThreadName("xrpld: grpc");
            this->impl_.handleRpcs();
        });
    }
    return running_;
}

void
GRPCServer::stop()
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
    XRPL_ASSERT(!running_, "xrpl::GRPCServer::~GRPCServer : is not running");
}

boost::asio::ip::tcp::endpoint
GRPCServer::getEndpoint() const
{
    return impl_.getEndpoint();
}

}  // namespace xrpl
