#include <helpers/DebugSink.h>

#include <iostream>

namespace xrpl {

DebugSink::DebugSink(beast::severities::Severity threshold)
    : Sink(threshold, false)
{
}

void
DebugSink::write(beast::severities::Severity level, std::string const& text)
{
    if (level < threshold())
        return;
    writeAlways(level, text);
}

void
DebugSink::writeAlways(
    beast::severities::Severity level,
    std::string const& text)
{
    std::cerr << text << std::endl;
}

}  // namespace xrpl
