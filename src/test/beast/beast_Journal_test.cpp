#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/utility/Journal.h>

namespace beast {

class Journal_test : public unit_test::suite
{
public:
    class TestSink : public Journal::Sink
    {
    private:
        int count_;

    public:
        TestSink() : Sink(severities::kWarning, false), count_(0)
        {
        }

        int
        count() const
        {
            return count_;
        }

        void
        reset()
        {
            count_ = 0;
        }

        void
        write(severities::Severity level, std::string const&) override
        {
            if (level >= threshold())
                ++count_;
        }

        void
        writeAlways(severities::Severity level, std::string const&) override
        {
            ++count_;
        }
    };

    void
    run() override
    {
        TestSink sink;

        using namespace beast::severities;
        sink.threshold(kInfo);

        Journal j(sink);

        j.trace() << " ";
        BEAST_EXPECT(sink.count() == 0);
        j.debug() << " ";
        BEAST_EXPECT(sink.count() == 0);
        j.info() << " ";
        BEAST_EXPECT(sink.count() == 1);
        j.warn() << " ";
        BEAST_EXPECT(sink.count() == 2);
        j.error() << " ";
        BEAST_EXPECT(sink.count() == 3);
        j.fatal() << " ";
        BEAST_EXPECT(sink.count() == 4);

        sink.reset();

        sink.threshold(kDebug);

        j.trace() << " ";
        BEAST_EXPECT(sink.count() == 0);
        j.debug() << " ";
        BEAST_EXPECT(sink.count() == 1);
        j.info() << " ";
        BEAST_EXPECT(sink.count() == 2);
        j.warn() << " ";
        BEAST_EXPECT(sink.count() == 3);
        j.error() << " ";
        BEAST_EXPECT(sink.count() == 4);
        j.fatal() << " ";
        BEAST_EXPECT(sink.count() == 5);
    }
};

BEAST_DEFINE_TESTSUITE(Journal, beast, beast);

}  // namespace beast
