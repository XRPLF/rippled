#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <xrpl/beast/unit_test.h>

namespace ripple {
namespace test {

class WSClient_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        {
            Json::Value jv;
            jv["streams"] = Json::arrayValue;
            jv["streams"].append("ledger");
        }
        env.fund(XRP(10000), "alice");
        env.close();
        auto jv = wsc->getMsg(std::chrono::seconds(1));
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(WSClient, jtx, ripple);

}  // namespace test
}  // namespace ripple
