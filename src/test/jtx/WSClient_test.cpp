#include <test/jtx/Env.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>

#include <chrono>

namespace xrpl::test {

class WSClient_test : public beast::unit_test::Suite
{
public:
    void
    testSmoke()
    {
        testcase("smoke");
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        {
            json::Value jv;
            jv["streams"] = json::ValueType::Array;
            jv["streams"].append("ledger");
        }
        env.fund(XRP(10000), "alice");
        env.close();
        auto jv = wsc->getMsg(std::chrono::seconds(1));
        pass();
    }

    void
    testGracefulDisconnect()
    {
        testcase("graceful disconnect");
        using namespace jtx;
        using namespace std::chrono;

        Env env(*this);
        auto wsc = makeWSClient(env.app().config());

        // Put real traffic on the connection before closing it.
        json::Value stream;
        stream["streams"] = json::ValueType::Array;
        stream["streams"].append("ledger");
        auto const sub = wsc->invoke("subscribe", stream);
        BEAST_EXPECT(sub.isMember("result") || sub.isMember("status"));

        // disconnect() performs a graceful WebSocket closing handshake and
        // blocks until the server acknowledges. On loopback that completes in
        // well under its internal 1s timeout; only a broken async_close/ack
        // coordination would fall through to the force-close path at ~1s. A
        // generous bound keeps this from flaking under load while still
        // catching that regression.
        auto const start = steady_clock::now();
        wsc->disconnect();
        auto const elapsed = duration_cast<milliseconds>(steady_clock::now() - start);
        BEAST_EXPECT(elapsed < milliseconds{750});

        // disconnect() must be idempotent: a second call (and the subsequent
        // destructor) must not hang, double-close, or crash.
        wsc->disconnect();
        pass();
    }

    void
    run() override
    {
        testSmoke();
        testGracefulDisconnect();
    }
};

BEAST_DEFINE_TESTSUITE(WSClient, jtx, xrpl);

}  // namespace xrpl::test
