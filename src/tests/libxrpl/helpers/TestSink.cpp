#include <helpers/TestSink.h>

#include <iostream>

namespace xrpl {

TestSink::TestSink(beast::severities::Severity threshold)
    : Sink(threshold, false)
{
}

void
TestSink::write(beast::severities::Severity level, std::string const& text)
{
    if (level < threshold())
        return;
    writeAlways(level, text);
}

void
TestSink::writeAlways(
    beast::severities::Severity level,
    std::string const& text)
{
    std::cerr << text << std::endl;
}

}  // namespace xrpl
