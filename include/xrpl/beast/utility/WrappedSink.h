#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <utility>

namespace beast {

/** Wraps a Journal::Sink to prefix its output with a string. */

// A WrappedSink both is a Sink and has a Sink:
//   o It inherits from Sink so it has the correct interface.
//   o It has a sink (reference) so it preserves the passed write() behavior.
// The data inherited from the base class is ignored.
class WrappedSink : public beast::Journal::Sink
{
private:
    beast::Journal::Sink& sink_;
    std::string prefix_;

public:
    explicit WrappedSink(beast::Journal::Sink& sink, std::string prefix = "")
        : Sink(sink), sink_(sink), prefix_(std::move(prefix))
    {
    }

    explicit WrappedSink(beast::Journal const& journal, std::string const& prefix = "")
        : WrappedSink(journal.sink(), prefix)
    {
    }

    void
    prefix(std::string const& s)
    {
        prefix_ = s;
    }

    [[nodiscard]] bool
    active(beast::severities::Severity level) const override
    {
        return sink_.active(level);
    }

    [[nodiscard]] bool
    console() const override
    {
        return sink_.console();
    }

    void
    console(bool output) override
    {
        sink_.console(output);
    }

    [[nodiscard]] beast::severities::Severity
    threshold() const override
    {
        return sink_.threshold();
    }

    void
    threshold(beast::severities::Severity thresh) override
    {
        sink_.threshold(thresh);
    }

    void
    write(beast::severities::Severity level, std::string const& text) override
    {
        using beast::Journal;
        sink_.write(level, prefix_ + text);
    }

    void
    writeAlways(severities::Severity level, std::string const& text) override
    {
        using beast::Journal;
        sink_.writeAlways(level, prefix_ + text);
    }
};

}  // namespace beast
