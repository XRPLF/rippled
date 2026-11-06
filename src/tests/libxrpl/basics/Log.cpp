#include <xrpl/basics/Log.h>

#include <xrpl/beast/utility/Journal.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace xrpl;

namespace {

class CountingLogs : public Logs
{
public:
    int made = 0;

    CountingLogs() : Logs(beast::Severity::Warning)
    {
    }

    std::unique_ptr<beast::Journal::Sink>
    makeSink(std::string const& partition, beast::Severity threshold) override
    {
        ++made;
        return Logs::makeSink(partition, threshold);
    }
};

}  // namespace

TEST(LogsTest, get_makes_one_sink_per_partition)
{
    CountingLogs logs;
    beast::Journal::Sink const& first = logs.get("View");
    EXPECT_EQ(logs.made, 1);

    // Same partition, any case: same sink, and no new one is built.
    EXPECT_EQ(&logs.get("View"), &first);
    EXPECT_EQ(&logs.get("view"), &first);
    EXPECT_EQ(logs.made, 1);

    EXPECT_NE(&logs.get("Ledger"), &first);
    EXPECT_EQ(logs.made, 2);
}
