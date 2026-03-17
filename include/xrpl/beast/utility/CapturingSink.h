#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <mutex>
#include <string>
#include <vector>

namespace beast {

/** A Journal::Sink that captures log messages to a buffer.
 *
 * This sink stores all log messages written to it and optionally
 * forwards them to another sink. Useful for capturing transaction
 * processing logs to return in RPC responses for debugging.
 */
class CapturingSink : public beast::Journal::Sink
{
public:
    struct Entry
    {
        severities::Severity level;
        std::string text;
    };

private:
    beast::Journal::Sink* forwardSink_;
    std::vector<Entry> entries_;
    mutable std::mutex mutex_;

public:
    /** Construct a CapturingSink.
     *
     * @param forwardSink Optional sink to forward messages to. Pass nullptr
     *        to only capture without forwarding.
     * @param thresh The minimum severity level to capture.
     */
    explicit CapturingSink(
        beast::Journal::Sink* forwardSink = nullptr,
        severities::Severity thresh = severities::kTrace)
        : Sink(thresh, false), forwardSink_(forwardSink)
    {
    }

    /** Construct a CapturingSink that forwards to a Journal's sink.
     *
     * @param journal Journal whose sink will receive forwarded messages.
     * @param thresh The minimum severity level to capture.
     */
    explicit CapturingSink(
        beast::Journal const& journal,
        severities::Severity thresh = severities::kTrace)
        : CapturingSink(&journal.sink(), thresh)
    {
    }

    /** Set or change the forward sink.
     *
     * @param sink The sink to forward messages to, or nullptr to disable.
     */
    void
    setForwardSink(beast::Journal::Sink* sink)
    {
        forwardSink_ = sink;
    }

    bool
    active(severities::Severity level) const override
    {
        // Always active at trace level to capture all messages
        // But also check forward sink
        if (forwardSink_ && forwardSink_->active(level))
            return true;
        return level >= threshold();
    }

    bool
    console() const override
    {
        return forwardSink_ ? forwardSink_->console() : false;
    }

    void
    console(bool output) override
    {
        if (forwardSink_)
            forwardSink_->console(output);
    }

    void
    write(severities::Severity level, std::string const& text) override
    {
        if (level < threshold())
            return;
        writeAlways(level, text);
    }

    void
    writeAlways(severities::Severity level, std::string const& text) override
    {
        {
            std::lock_guard lock(mutex_);
            entries_.push_back({level, text});
        }

        if (forwardSink_)
            forwardSink_->writeAlways(level, text);
    }

    /** Get all captured log entries. */
    std::vector<Entry>
    getEntries() const
    {
        std::lock_guard lock(mutex_);
        return entries_;
    }

    /** Clear all captured entries. */
    void
    clear()
    {
        std::lock_guard lock(mutex_);
        entries_.clear();
    }

    /** Get the number of captured entries. */
    std::size_t
    size() const
    {
        std::lock_guard lock(mutex_);
        return entries_.size();
    }

    /** Convert severity level to string for JSON output. */
    static std::string
    severityToString(severities::Severity level)
    {
        switch (level)
        {
            case severities::kTrace:
                return "trace";
            case severities::kDebug:
                return "debug";
            case severities::kInfo:
                return "info";
            case severities::kWarning:
                return "warning";
            case severities::kError:
                return "error";
            case severities::kFatal:
                return "fatal";
            default:
                return "unknown";
        }
    }
};

}  // namespace beast
