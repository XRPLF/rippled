#ifndef XRPL_DEBUGSINK_H
#define XRPL_DEBUGSINK_H

#include <xrpl/beast/utility/Journal.h>

namespace xrpl {
class DebugSink : public beast::Journal::Sink
{
public:
    static DebugSink&
    instance()
    {
        static DebugSink _;
        return _;
    }

    DebugSink(
        beast::severities::Severity threshold = beast::severities::kDebug);

    void
    write(beast::severities::Severity level, std::string const& text) override;

    void
    writeAlways(beast::severities::Severity level, std::string const& text)
        override;
};
}  // namespace xrpl
#endif  // XRPL_DEBUGSINK_H
