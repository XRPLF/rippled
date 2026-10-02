#include <xrpl/beast/utility/Journal.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapMissingNode.h>

#include <gtest/gtest.h>
#include <helpers/TestSink.h>
#include <shamap/common.h>

#include <memory>

namespace xrpl::tests {

TEST(FetchPackTest, construct_table)
{
    beast::Journal const j{TestSink::instance()};
    TestNodeFamily f{j};
    std::shared_ptr<SHAMap> const t1{std::make_shared<SHAMap>(SHAMapType::FREE, f)};

    EXPECT_NE(t1, nullptr);
}

}  // namespace xrpl::tests
