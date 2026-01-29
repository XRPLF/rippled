#ifndef XRPL_DEBUGSINK_H
#define XRPL_DEBUGSINK_H

#include <xrpl/beast/utility/Journal.h>

namespace xrpl {
class TestSink : public beast::Journal::Sink
{
public:
    static TestSink&
    instance()
    {
        static TestSink sink{};
        return sink;
    }

    TestSink(beast::severities::Severity threshold = beast::severities::kDebug);

    void
    write(beast::severities::Severity level, std::string const& text) override;

    void
    writeAlways(beast::severities::Severity level, std::string const& text) override;
};
}  // namespace xrpl
#endif  // XRPL_DEBUGSINK_H
