#include <test/jtx/TestHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/JsonRpc.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

/**
 * Pins the rule by which a reply's envelope is recognized.
 *
 * `isSpecEnvelope` is read by `rpcPayload` and `rpcLegacyReply`, which dozens
 * of assertions across the suites index through. A misreading there does not
 * fail loudly: it hands every one of those assertions a payload from the wrong
 * place, so they compare a null against a null and pass. The rule is also the
 * opposite of obvious, no single member identifying the envelope, so it is
 * pinned here against replies written out rather than obtained from a server.
 *
 * A server-driven test cannot do this job. It can only show the helper agreeing
 * with the envelopes the server happens to send, where what needs holding is
 * that the near misses are told apart: a legacy reply that echoes `jsonrpc`,
 * and a ripplerpc version 2 error, which is an object under `error` as well.
 */
class TestHelpers_test : public beast::unit_test::Suite
{
    /**
     * A version 3 failure: {code, message} under `error`, and no legacy member.
     *
     * @return The reply, as a client receives it.
     */
    static json::Value
    specError()
    {
        json::Value reply(json::ValueType::Object);
        reply[jss::jsonrpc] = rpc::kJsonRpcVersion;
        reply[jss::id] = 1;
        reply[jss::error] = json::ValueType::Object;
        reply[jss::error][jss::code] = rpc::kJsonRpcInvalidRequest;
        reply[jss::error][jss::message] = "Request is not a JSON object";
        return reply;
    }

    /**
     * A version 3 success: a `result` with no `status` at either level.
     *
     * @return The reply, as a client receives it.
     */
    static json::Value
    specSuccess()
    {
        json::Value reply(json::ValueType::Object);
        reply[jss::jsonrpc] = rpc::kJsonRpcVersion;
        reply[jss::id] = 2;
        reply[jss::result] = json::ValueType::Object;
        reply[jss::result][jss::ledger_index] = 3;
        return reply;
    }

    /**
     * A `ripplerpc: "2.0"` failure as `shapeReply` writes it: the legacy payload
     * under `error`, with `code` and `message` beside the legacy members and no
     * `error_message`.
     *
     * @return The reply, as a client receives it.
     */
    static json::Value
    ripplerpcError()
    {
        json::Value reply(json::ValueType::Object);
        reply[jss::ripplerpc] = "2.0";
        reply[jss::error] = json::ValueType::Object;
        reply[jss::error][jss::status] = jss::error;
        reply[jss::error][jss::error] = "invalidParams";
        reply[jss::error][jss::error_code] = RpcInvalidParams;
        reply[jss::error][jss::code] = RpcInvalidParams;
        reply[jss::error][jss::message] = "Invalid parameters.";
        return reply;
    }

public:
    /**
     * A version 3 failure and a version 3 success are both recognized.
     */
    void
    testSpecEnvelopeIsRecognized()
    {
        testcase("The specification envelope is recognized");

        using namespace jtx;

        BEAST_EXPECT(isSpecEnvelope(specError()));
        BEAST_EXPECT(isSpecEnvelope(specSuccess()));
    }

    /**
     * The three near misses read as legacy: a `status` at either level, an
     * echoed `jsonrpc`, and the ripplerpc version 2 error object.
     */
    void
    testLegacyEnvelopeIsNotMistakenForIt()
    {
        testcase("A legacy envelope is not mistaken for the specification");

        using namespace jtx;

        // The `status` a legacy reply always writes, at whichever level.
        {
            json::Value reply(json::ValueType::Object);
            reply[jss::result] = json::ValueType::Object;
            reply[jss::result][jss::status] = jss::success;
            BEAST_EXPECTS(!isSpecEnvelope(reply), to_string(reply));

            json::Value outer(json::ValueType::Object);
            outer[jss::status] = jss::success;
            outer[jss::result] = json::ValueType::Object;
            BEAST_EXPECTS(!isSpecEnvelope(outer), to_string(outer));
        }

        // A legacy WebSocket reply echoes the `jsonrpc` its request sent, so that member alone
        // decides nothing. This is the near miss the docstring names first.
        {
            json::Value reply(json::ValueType::Object);
            reply[jss::jsonrpc] = rpc::kJsonRpcVersion;
            reply[jss::status] = jss::success;
            reply[jss::type] = jss::response;
            reply[jss::result] = json::ValueType::Object;
            BEAST_EXPECTS(!isSpecEnvelope(reply), to_string(reply));
        }

        // The ripplerpc version 2 error envelope moves the whole result under `error`, so an object
        // there decides nothing either. It is told apart by the two members it keeps.
        {
            json::Value reply(json::ValueType::Object);
            reply[jss::error] = json::ValueType::Object;
            reply[jss::error][jss::error] = "invalidParams";
            reply[jss::error][jss::error_code] = RpcInvalidParams;
            reply[jss::error][jss::code] = RpcInvalidParams;
            reply[jss::error][jss::message] = "Invalid parameters.";
            reply[jss::error][jss::status] = jss::error;
            BEAST_EXPECTS(!isSpecEnvelope(reply), to_string(reply));
        }
    }

    /**
     * `rpcPayload` hands a caller the legacy payload shape from either
     * envelope, with and without `data`, restores `error_message` to a
     * `ripplerpc` failure, and leaves a version 1 reply as it is.
     */
    void
    testPayloadIsOneShapeAcrossEnvelopes()
    {
        testcase("A payload reads the same whichever envelope carried it");

        using namespace jtx;

        // A specification failure carries the token and code inside `data`, which is what a caller
        // asserting on the legacy shape expects to find at the top level of the payload.
        {
            auto reply = specError();
            reply[jss::error][jss::data] = json::ValueType::Object;
            reply[jss::error][jss::data][jss::error] = "invalidParams";
            reply[jss::error][jss::data][jss::error_code] = RpcInvalidParams;

            auto const payload = rpcPayload(reply);
            BEAST_EXPECTS(payload[jss::error] == "invalidParams", to_string(payload));
            BEAST_EXPECTS(payload[jss::error_code] == RpcInvalidParams, to_string(payload));
            BEAST_EXPECTS(
                payload[jss::error_message] == "Request is not a JSON object", to_string(payload));
            BEAST_EXPECTS(payload[jss::status] == jss::error, to_string(payload));
        }

        // A pre-dispatch rejection carries no `data`, the specification making it optional. The
        // payload is then the message and the status alone rather than a null.
        {
            auto const payload = rpcPayload(specError());
            BEAST_EXPECTS(!payload.isMember(jss::error), to_string(payload));
            BEAST_EXPECTS(
                payload[jss::error_message] == "Request is not a JSON object", to_string(payload));
            BEAST_EXPECTS(payload[jss::status] == jss::error, to_string(payload));
        }

        // A success regains the `status` the specification has no place for.
        {
            auto const payload = rpcPayload(specSuccess());
            BEAST_EXPECTS(payload[jss::ledger_index] == 3, to_string(payload));
            BEAST_EXPECTS(payload[jss::status] == jss::success, to_string(payload));
        }

        // A legacy reply is returned as it stands, so a caller reads one shape either way.
        {
            json::Value reply(json::ValueType::Object);
            reply[jss::result] = json::ValueType::Object;
            reply[jss::result][jss::status] = jss::success;
            reply[jss::result][jss::ledger_index] = 3;
            BEAST_EXPECT(rpcPayload(reply) == reply[jss::result]);
        }

        // A `ripplerpc` 2.0 or 3.0 failure sits under `error` with `message` in place of
        // `error_message`. The payload regains that name, so it reads as a version 1 failure.
        {
            auto const payload = rpcPayload(ripplerpcError());
            BEAST_EXPECTS(payload[jss::error] == "invalidParams", to_string(payload));
            BEAST_EXPECTS(payload[jss::error_code] == RpcInvalidParams, to_string(payload));
            BEAST_EXPECTS(payload[jss::error_message] == "Invalid parameters.", to_string(payload));
            BEAST_EXPECTS(payload[jss::status] == jss::error, to_string(payload));
        }
    }

    /**
     * `rpcLegacyReply` keeps the outer legacy shape, `status` beside `result`,
     * moves a `ripplerpc` failure under `result`, and returns a version 1
     * reply unchanged.
     */
    void
    testLegacyReplyKeepsTheOuterShape()
    {
        testcase("A rewritten reply keeps the outer legacy shape");

        using namespace jtx;

        auto const legacy = rpcLegacyReply(specSuccess());
        BEAST_EXPECTS(legacy[jss::status] == jss::success, to_string(legacy));
        BEAST_EXPECTS(legacy[jss::result][jss::ledger_index] == 3, to_string(legacy));

        auto const failed = rpcLegacyReply(specError());
        BEAST_EXPECTS(failed[jss::status] == jss::error, to_string(failed));
        BEAST_EXPECTS(
            failed[jss::result][jss::error_message] == "Request is not a JSON object",
            to_string(failed));

        // A reply already in the legacy envelope is returned unchanged, not rewritten twice.
        json::Value reply(json::ValueType::Object);
        reply[jss::status] = jss::success;
        reply[jss::result] = json::ValueType::Object;
        reply[jss::result][jss::status] = jss::success;
        BEAST_EXPECT(rpcLegacyReply(reply) == reply);

        // A `ripplerpc` failure moves from `error` to `result`, with `status` beside it.
        auto const moved = rpcLegacyReply(ripplerpcError());
        BEAST_EXPECTS(moved[jss::status] == jss::error, to_string(moved));
        BEAST_EXPECTS(moved[jss::result][jss::error] == "invalidParams", to_string(moved));
    }

    void
    run() override
    {
        testSpecEnvelopeIsRecognized();
        testLegacyEnvelopeIsNotMistakenForIt();
        testPayloadIsOneShapeAcrossEnvelopes();
        testLegacyReplyKeepsTheOuterShape();
    }
};

BEAST_DEFINE_TESTSUITE(TestHelpers, jtx, xrpl);

}  // namespace xrpl::test
