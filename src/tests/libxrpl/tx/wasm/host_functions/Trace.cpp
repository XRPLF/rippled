#include <xrpl/beast/utility/Journal.h>

#include <gtest/gtest.h>
#include <helpers/CaptureSink.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <string>

namespace xrpl::test {

struct TraceImpl : RealHostFixture
{
};

TEST_F(TraceImpl, LogsMessageAndData)
{
    auto h = makeTracingHost();
    h->trace("hello", "world");
    EXPECT_NE(logged().find("hello world"), std::string::npos) << logged();
}

TEST_F(TraceImpl, NothingLoggedBelowTraceSeverity)
{
    CaptureSink sink{beast::Severity::Error};
    auto h = makeHost(beast::Journal{sink});
    h->trace("hello", "world");
    EXPECT_TRUE(sink.messages().empty()) << sink.messages();
}

}  // namespace xrpl::test
