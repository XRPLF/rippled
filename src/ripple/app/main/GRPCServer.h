//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_GRPCSERVER_H_INCLUDED
#define RIPPLE_CORE_GRPCSERVER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/Stoppable.h>
#include <ripple/net/InfoSub.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/resource/Charge.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/Handler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

#include "org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h"
#include <grpcpp/grpcpp.h>

namespace ripple {

// Interface that CallData implements
class Processor
{
public:
    virtual ~Processor() = default;

    Processor() = default;

    Processor(const Processor&) = delete;

    Processor&
    operator=(const Processor&) = delete;

    // process a request that has arrived. Can only be called once per instance
    virtual void
    process() = 0;

    // create a new instance of this CallData object, with the same type
    //(same template parameters) as original. This is called when a CallData
    // object starts processing a request. Creating a new instance allows the
    // server to handle additional requests while the first is being processed
    virtual std::shared_ptr<Processor>
    clone() = 0;

    // true if this object has finished processing the request. Object will be
    // deleted once this function returns true
    virtual bool
    isFinished() = 0;
};

class GRPCServerImpl final
{
private:
    // CompletionQueue returns events that have occurred, or events that have
    // been cancelled
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;

    std::vector<std::shared_ptr<Processor>> requests_;

    // The gRPC service defined by the .proto files
    org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService service_;

    std::unique_ptr<grpc::Server> server_;

    Application& app_;

    std::string serverAddress_;

    beast::Journal journal_;

    // typedef for function to bind a listener
    // This is always of the form:
    // org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::Request[RPC NAME]
    template <class Request, class Response>
    using BindListener = std::function<void(
        org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService&,
        grpc::ServerContext*,
        Request*,
        grpc::ServerAsyncResponseWriter<Response>*,
        grpc::CompletionQueue*,
        grpc::ServerCompletionQueue*,
        void*)>;

    // typedef for actual handler (that populates a response)
    // handlers are defined in rpc/GRPCHandlers.h
    template <class Request, class Response>
    using Handler = std::function<std::pair<Response, grpc::Status>(
        RPC::GRPCContext<Request>&)>;
    // This implementation is currently limited to v1 of the API
    static unsigned constexpr apiVersion = 1;

public:
    explicit GRPCServerImpl(Application& app);

    GRPCServerImpl(const GRPCServerImpl&) = delete;

    GRPCServerImpl&
    operator=(const GRPCServerImpl&) = delete;

    void
    shutdown();

    // setup the server and listeners
    // returns true if server started successfully
    bool
    start();

    // the main event loop
    void
    handleRpcs();

    // Create a CallData object for each RPC. Return created objects in vector
    std::vector<std::shared_ptr<Processor>>
    setupListeners();

private:
    // Class encompasing the state and logic needed to serve a request.
    template <class Request, class Response>
    class CallData
        : public Processor,
          public std::enable_shared_from_this<CallData<Request, Response>>
    {
    private:
        // The means of communication with the gRPC runtime for an asynchronous
        // server.
        org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService& service_;

        // The producer-consumer queue for asynchronous server notifications.
        grpc::ServerCompletionQueue& cq_;

        // Context for the rpc, allowing to tweak aspects of it such as the use
        // of compression, authentication, as well as to send metadata back to
        // the client.
        grpc::ServerContext ctx_;

        // true if finished processing request
        // Note, this variable does not need to be atomic, since it is
        // currently only accessed from one thread. However, isFinished(),
        // which returns the value of this variable, is public facing. In the
        // interest of avoiding future concurrency bugs, we make it atomic.
        std::atomic_bool finished_;

        Application& app_;

        // What we get from the client.
        Request request_;

        // What we send back to the client.
        Response reply_;

        // The means to get back to the client.
        grpc::ServerAsyncResponseWriter<Response> responder_;

        // Function that creates a listener for specific request type
        BindListener<Request, Response> bindListener_;

        // Function that processes a request
        Handler<Request, Response> handler_;

        // Condition required for this RPC
        RPC::Condition requiredCondition_;

        // Load type for this RPC
        Resource::Charge loadType_;

    public:
        virtual ~CallData() = default;

        // Take in the "service" instance (in this case representing an
        // asynchronous server) and the completion queue "cq" used for
        // asynchronous communication with the gRPC runtime.
        explicit CallData(
            org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService& service,
            grpc::ServerCompletionQueue& cq,
            Application& app,
            BindListener<Request, Response> bindListener,
            Handler<Request, Response> handler,
            RPC::Condition requiredCondition,
            Resource::Charge loadType);

        CallData(const CallData&) = delete;

        CallData&
        operator=(const CallData&) = delete;

        virtual void
        process() override;

        virtual bool
        isFinished() override;

        std::shared_ptr<Processor>
        clone() override;

    private:
        // process the request. Called inside the coroutine passed to JobQueue
        void
        process(std::shared_ptr<JobQueue::Coro> coro);

        // return load type of this RPC
        Resource::Charge
        getLoadType();

        // return the Role required for this RPC
        // for now, we are only supporting RPC's that require Role::USER for
        // gRPC
        Role
        getRole();

        // register endpoint with ResourceManager and return usage
        Resource::Consumer
        getUsage();

    };  // CallData

};  // GRPCServerImpl

class GRPCServer : public Stoppable
{
public:
    explicit GRPCServer(Application& app, Stoppable& parent)
        : Stoppable("GRPCServer", parent), impl_(app)
    {
    }

    GRPCServer(const GRPCServer&) = delete;

    GRPCServer&
    operator=(const GRPCServer&) = delete;

    void
    onStart() override;

    void
    onStop() override;

    ~GRPCServer(){};

private:
    GRPCServerImpl impl_;
    std::thread thread_;
    bool running_;
};
}  // namespace ripple
#endif
