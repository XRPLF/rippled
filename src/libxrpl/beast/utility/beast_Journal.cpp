#include <xrpl/beast/utility/Journal.h>

#include <ios>
#include <ostream>
#include <string>

namespace beast {

//------------------------------------------------------------------------------

// A Sink that does nothing.
class NullJournalSink : public Journal::Sink
{
public:
    NullJournalSink() : Sink(Severity::Disabled, false)
    {
    }

    ~NullJournalSink() override = default;

    [[nodiscard]] bool
    active(Severity) const override
    {
        return false;
    }

    [[nodiscard]] bool
    console() const override
    {
        return false;
    }

    void
    console(bool) override
    {
    }

    [[nodiscard]] Severity
    threshold() const override
    {
        return Severity::Disabled;
    }

    void
    threshold(Severity) override
    {
    }

    void
    write(Severity, std::string const&) override
    {
    }

    void
    writeAlways(Severity, std::string const&) override
    {
    }
};

//------------------------------------------------------------------------------

Journal::Sink&
Journal::getNullSink()
{
    static NullJournalSink kSink;
    return kSink;
}

//------------------------------------------------------------------------------

Journal::Sink::Sink(Severity thresh, bool console) : thresh_(thresh), console_(console)
{
}

Journal::Sink::~Sink() = default;

bool
Journal::Sink::active(Severity level) const
{
    return level >= thresh_;
}

bool
Journal::Sink::console() const
{
    return console_;
}

void
Journal::Sink::console(bool output)
{
    console_ = output;
}

Severity
Journal::Sink::threshold() const
{
    return thresh_;
}

void
Journal::Sink::threshold(Severity thresh)
{
    thresh_ = thresh;
}

//------------------------------------------------------------------------------

Journal::ScopedStream::ScopedStream(Sink& sink, Severity level) : sink_(sink), level_(level)
{
    // Modifiers applied from all ctors
    ostream_ << std::boolalpha << std::showbase;
}

Journal::ScopedStream::ScopedStream(Stream const& stream, std::ostream& manip(std::ostream&))
    : ScopedStream(stream.sink(), stream.level())
{
    ostream_ << manip;
}

Journal::ScopedStream::~ScopedStream()
{
    std::string const& s(ostream_.str());
    if (!s.empty())
    {
        if (s == "\n")
        {
            sink_.write(level_, "");
        }
        else
        {
            sink_.write(level_, s);
        }
    }
}

std::ostream&
Journal::ScopedStream::operator<<(std::ostream& manip(std::ostream&)) const
{
    return ostream_ << manip;
}

//------------------------------------------------------------------------------

Journal::ScopedStream
Journal::Stream::operator<<(std::ostream& manip(std::ostream&)) const
{
    return ScopedStream(*this, manip);
}

}  // namespace beast
