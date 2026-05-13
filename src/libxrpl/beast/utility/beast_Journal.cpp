/** @file
 *  Out-of-line definitions for the beast::Journal logging framework.
 *
 *  Defines `NullJournalSink` (the Null Object for `Journal::Sink`),
 *  `Journal::getNullSink()`, the base-class method bodies for `Journal::Sink`,
 *  and the RAII `Journal::ScopedStream` constructors and destructor.
 *  The concrete, file-writing sink lives in `src/libxrpl/basics/Log.cpp`.
 */
#include <xrpl/beast/utility/Journal.h>

#include <ios>
#include <ostream>
#include <string>

namespace beast {

//------------------------------------------------------------------------------

/** Null Object implementation of `Journal::Sink` that silently discards all messages.
 *
 *  Initialised with `Severity::Disabled` so that `active()` unconditionally
 *  returns `false` and every write operation is a no-op. The single shared
 *  instance is returned by `Journal::getNullSink()`, providing a valid sink
 *  for default-constructed `Journal::Stream` objects without requiring callers
 *  to guard against null pointers.
 *
 *  @note Threshold mutations via `threshold(Severity)` are silently ignored;
 *      the sink is permanently disabled regardless of runtime configuration.
 */
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

/** Return the process-wide null sink singleton.
 *
 *  The instance is a function-local static, so C++11 guarantees thread-safe
 *  initialisation with no mutex required. Its static lifetime ensures it
 *  outlives any `Journal::Stream` that holds a reference to it.
 *
 *  @return A reference to the shared `NullJournalSink` instance.
 */
Journal::Sink&
Journal::getNullSink()
{
    static NullJournalSink kSINK;
    return kSINK;
}

//------------------------------------------------------------------------------

/** Construct a Sink with the given severity threshold and console flag.
 *
 *  @param thresh  Minimum severity level at which `active()` returns `true`.
 *  @param console Initial value for the console-output flag.
 */
Journal::Sink::Sink(Severity thresh, bool console) : thresh_(thresh), console_(console)
{
}

Journal::Sink::~Sink() = default;

/** Returns `true` if `level` is at or above the current threshold.
 *
 *  This is the hot-path gate: callers should invoke it before performing any
 *  string formatting so that disabled log levels incur no allocation cost.
 *
 *  @param level  The severity of the candidate message.
 *  @return `true` if the message should be written; `false` if it would be suppressed.
 */
bool
Journal::Sink::active(Severity level) const
{
    return level >= thresh_;
}

/** Returns `true` if messages are also forwarded to the MSVC Output Window. */
bool
Journal::Sink::console() const
{
    return console_;
}

/** Set whether messages are also forwarded to the MSVC Output Window.
 *
 *  @param output `true` to enable console output; `false` to disable.
 */
void
Journal::Sink::console(bool output)
{
    console_ = output;
}

/** Returns the minimum severity level this sink will report. */
Severity
Journal::Sink::threshold() const
{
    return thresh_;
}

/** Set the minimum severity level this sink will report.
 *
 *  Messages below this threshold are suppressed by `active()`.
 *  The admin interface in `Logs` calls this to support runtime log-level
 *  changes without restarting the server.
 *
 *  @param thresh The new minimum severity.
 */
void
Journal::Sink::threshold(Severity thresh)
{
    thresh_ = thresh;
}

//------------------------------------------------------------------------------

/** Construct a ScopedStream targeting `sink` at the given `level`.
 *
 *  Applies `std::boolalpha` and `std::showbase` to the internal
 *  `ostringstream` immediately, so every rippled log message prints booleans
 *  as `true`/`false` and hex values with a `0x` prefix without per-callsite
 *  effort.
 *
 *  @param sink   The destination sink that will receive the completed message.
 *  @param level  The severity at which the message will be written.
 */
Journal::ScopedStream::ScopedStream(Sink& sink, Severity level) : sink_(sink), level_(level)
{
    ostream_ << std::boolalpha << std::showbase;
}

/** Construct a ScopedStream from a Stream with an initial ostream manipulator.
 *
 *  Delegates to the primary constructor (which applies `boolalpha`/`showbase`)
 *  and then immediately applies `manip` to the buffer. This is the entry point
 *  when `Journal::Stream::operator<<` is called with a manipulator such as
 *  `std::hex`; further `<<` chaining continues into the same buffer.
 *
 *  @param stream  The originating Stream providing the sink and severity level.
 *  @param manip   An `std::ostream` manipulator to apply before accumulating
 *      the message body (e.g. `std::hex`, `std::setw`).
 */
Journal::ScopedStream::ScopedStream(Stream const& stream, std::ostream& manip(std::ostream&))
    : ScopedStream(stream.sink(), stream.level())
{
    ostream_ << manip;
}

/** Flush the accumulated message to the sink.
 *
 *  Delivers the complete buffered string to `sink_.write()` atomically,
 *  preventing interleaved output from concurrent threads in sinks that
 *  serialize under a mutex (e.g. `Logs::Sink`).
 *
 *  @note A bare `operator<<(std::endl)` produces a buffer containing only
 *      `"\n"`. The destructor maps this to an empty string so that such a
 *      call does not generate a severity-tagged blank line in the output.
 */
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

/** Apply an ostream manipulator to the accumulated buffer.
 *
 *  @param manip  The manipulator to apply (e.g. `std::hex`, `std::setfill`).
 *  @return A reference to the underlying `ostringstream` for further chaining.
 */
std::ostream&
Journal::ScopedStream::operator<<(std::ostream& manip(std::ostream&)) const
{
    return ostream_ << manip;
}

//------------------------------------------------------------------------------

/** Construct a ScopedStream from this Stream with a pre-applied manipulator.
 *
 *  Defined here rather than inline in the header because it constructs a
 *  `ScopedStream` by value, which requires `ScopedStream` to be a complete
 *  type at the call site. The template `operator<<` overloads are defined
 *  inline in the header and do not have this completeness requirement.
 *
 *  @param manip  An ostream manipulator (e.g. `std::hex`) applied before
 *      the rest of the `<<` chain accumulates into the buffer.
 *  @return A new `ScopedStream` with `manip` already applied.
 */
Journal::ScopedStream
Journal::Stream::operator<<(std::ostream& manip(std::ostream&)) const
{
    return ScopedStream(*this, manip);
}

}  // namespace beast
