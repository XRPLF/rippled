#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/scope.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>

#include <exception>
#include <future>
#include <string>

namespace xrpl::test {

/**
 * Checks the error a busy server reports for a request it never dispatches.
 *
 *   RPCHandler_test ──doCommand()──> RPC::fillHandler()
 *          │                                │
 *          └── fills ──> JobQueue <── reads ─┘
 *
 * An overloaded server answers rpcTOO_BUSY before it reads the command name, so
 * the request fields still hold whatever json type the client sent. Anything
 * that runs afterwards to describe the error has to cope with that and leave
 * the answer alone.
 *
 * @note Each testcase keeps one job-queue worker blocked for as long as it
 * runs, and releases it before returning.
 */
class RPCHandler_test : public beast::unit_test::Suite
{
    /**
     * How many jobs to queue to hold the server over its overload threshold.
     * One job is dispatched straight away, so one spare keeps the waiting
     * count above the limit.
     */
    static constexpr int kOverloadJobs = rpc::tuning::kMaxJobQueueClients + 2;

    /**
     * Dispatches one request on an overloaded server and checks the client is
     * told the server is busy.
     *
     * @param params Request fields, in the form fillHandler() reads them.
     */
    void
    expectTooBusy(json::Value const& params)
    {
        using namespace jtx;
        Env env{*this};
        auto& app = env.app();

        // Only one job of this type runs at a time, so every job after the
        // first stays queued until the gate opens. They also sort above
        // JtClient, the priority the overload check counts from.
        std::promise<void> gate;
        std::shared_future<void> const open = gate.get_future().share();
        ScopeExit const openGate{[&gate]() { gate.set_value(); }};

        int queued = 0;
        for (int i = 0; i < kOverloadJobs; ++i)
        {
            if (app.getJobQueue().addJob(JtSweep, "overload", [open]() { open.wait(); }))
                ++queued;
        }
        BEAST_EXPECT(queued == kOverloadJobs);
        BEAST_EXPECT(app.getJobQueue().getJobCountGE(JtClient) > rpc::tuning::kMaxJobQueueClients);

        resource::Charge loadType = resource::kFeeReferenceRpc;
        resource::Consumer consumer;
        rpc::JsonContext context{
            {.j = env.journal,
             .app = app,
             .loadType = loadType,
             .netOps = app.getOPs(),
             .ledgerMaster = app.getLedgerMaster(),
             .consumer = consumer,
             .role = Role::USER,
             .coro = {},
             .infoSub = {},
             .apiVersion = rpc::kApiVersionIfUnspecified},
            params,
            {}};

        json::Value result;
        rpc::Status status;
        std::string thrown;
        try
        {
            status = rpc::doCommand(context, result);
        }
        catch (std::exception const& e)
        {
            thrown = e.what();
        }

        if (BEAST_EXPECTS(thrown.empty(), "doCommand threw: " + thrown))
        {
            BEAST_EXPECT(status.type() == rpc::Status::Type::ErrorCodeI);
            BEAST_EXPECT(status.toErrorCode() == RpcTooBusy);
            BEAST_EXPECT(result[jss::error].asString() == "tooBusy");
            BEAST_EXPECT(result[jss::error_code].asInt() == static_cast<int>(RpcTooBusy));
        }
    }

    /**
     * Checks a well-formed request on an overloaded server. This is the control
     * for the two cases below: it shares their fixture and their assertions,
     * and differs only in that every field it sends is a string.
     */
    void
    testRegisteredCommand()
    {
        testcase("Busy server, registered command");

        json::Value params = json::ValueType::Object;
        params[jss::command] = "ping";
        expectTooBusy(params);
    }

    /**
     * Checks a request whose "method" field is not a string.
     */
    void
    testNonStringMethod()
    {
        testcase("Busy server, method field is not a string");

        // The HTTP path sets "command" from the outer method name it has
        // already checked, and passes the inner request object through
        // untouched, so "method" can arrive holding any json type.
        json::Value params = json::ValueType::Object;
        params[jss::command] = "ping";
        params[jss::method] = json::ValueType::Array;
        expectTooBusy(params);
    }

    /**
     * Checks a request whose "command" field is not a string.
     */
    void
    testNonStringCommand()
    {
        testcase("Busy server, command field is not a string");

        json::Value params = json::ValueType::Object;
        params[jss::command] = json::ValueType::Object;
        expectTooBusy(params);
    }

public:
    void
    run() override
    {
        testRegisteredCommand();
        testNonStringMethod();
        testNonStringCommand();
    }
};

BEAST_DEFINE_TESTSUITE(RPCHandler, rpc, xrpl);

}  // namespace xrpl::test
