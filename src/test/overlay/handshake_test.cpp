#include <xrpld/overlay/detail/Handshake.h>

#include <xrpl/beast/unit_test.h>

namespace ripple {

namespace test {

class handshake_test : public beast::unit_test::suite
{
public:
    handshake_test() = default;

    void
    testHandshake()
    {
        testcase("X-Protocol-Ctl");
        boost::beast::http::fields headers;
        headers.insert(
            "X-Protocol-Ctl",
            "feature1=v1,v2,v3; feature2=v4; feature3=10; feature4=1; "
            "feature5=v6");
        BEAST_EXPECT(!featureEnabled(headers, "feature1"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature1", "2"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v1"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v2"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v3"));
        BEAST_EXPECT(isFeatureValue(headers, "feature2", "v4"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature3", "1"));
        BEAST_EXPECT(isFeatureValue(headers, "feature3", "10"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature4", "10"));
        BEAST_EXPECT(isFeatureValue(headers, "feature4", "1"));
        BEAST_EXPECT(!featureEnabled(headers, "v6"));
    }

    void
    run() override
    {
        testHandshake();
    }
};

BEAST_DEFINE_TESTSUITE(handshake, overlay, ripple);

}  // namespace test
}  // namespace ripple
