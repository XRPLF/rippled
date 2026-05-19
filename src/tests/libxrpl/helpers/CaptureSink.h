#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <sstream>
#include <string>

namespace xrpl::test {

class CaptureSink : public beast::Journal::Sink
{
    std::stringstream strm_;

public:
    explicit CaptureSink(beast::Severity threshold = beast::Severity::Debug)
        : Sink(threshold, false)
    {
    }

    void
    write(beast::Severity level, std::string const& text) override
    {
        if (level < threshold())
            return;
        writeAlways(level, text);
    }

    void
    writeAlways(beast::Severity /*level*/, std::string const& text) override
    {
        strm_ << text << std::endl;
    }

    [[nodiscard]] std::stringstream const&
    messages() const
    {
        return strm_;
    }
};

}  // namespace xrpl::test
