#include <test/jtx.h>
#include <test/jtx/Env.h>

#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <functional>

namespace xrpl {

class NFTBuyOffers_test : public beast::unit_test::suite
{
    void
    testBadInput()
    {
        testcase("Invalid request params");
        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->FEES.reference_fee = 10;
                    return cfg;
                })};

        {
            // no params
            auto const result = env.client().invoke("nft_buy_offers", {})[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            Json::Value params{Json::objectValue};
            params[jss::nft_id] = 20;
            auto const result = env.client().invoke("nft_sell_offers", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }
    }
public:
    void
    run() override
    {
        testBadInput();
    }
};

BEAST_DEFINE_TESTSUITE(NFTBuyOffers, rpc, xrpl);

}  // namespace xrpl
