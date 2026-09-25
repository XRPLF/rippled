#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <string>

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

    TestSink(beast::Severity threshold = beast::Severity::Debug);

    void
    write(beast::Severity level, std::string const& text) override;

    void
    writeAlways(beast::Severity level, std::string const& text) override;
};
}  // namespace xrpl
