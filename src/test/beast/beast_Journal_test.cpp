#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>

#include <string>

namespace beast {

class Journal_test : public unit_test::suite
{
public:
    class TestSink : public Journal::Sink
    {
    private:
        int m_count{0};

    public:
        TestSink() : Sink(severities::kWarning, false)
        {
        }

        [[nodiscard]] int
        count() const
        {
            return m_count;
        }

        void
        reset()
        {
            m_count = 0;
        }

        void
        write(severities::Severity level, std::string const&) override
        {
            if (level >= threshold())
                ++m_count;
        }

        void
        writeAlways(severities::Severity level, std::string const&) override
        {
            ++m_count;
        }
    };

    void
    run() override
    {
        TestSink sink;

        using namespace beast::severities;
        sink.threshold(kInfo);

        Journal const j(sink);

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
