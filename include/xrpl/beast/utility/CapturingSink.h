#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <mutex>
#include <string>
#include <vector>

namespace beast {

/**
 * A Journal::Sink that captures log messages to a buffer.
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
        Severity level;
        std::string text;
    };

private:
    beast::Journal::Sink* forwardSink_;
    std::vector<Entry> entries_;
    mutable std::mutex mutex_;

public:
    /**
     * Construct a CapturingSink.
     *
     * @param forwardSink Optional sink to forward messages to. Pass nullptr
     *        to only capture without forwarding.
     * @param thresh The minimum severity level to capture.
     */
    explicit CapturingSink(
        beast::Journal::Sink* forwardSink = nullptr,
        Severity thresh = Severity::Trace)
        : Sink(thresh, false), forwardSink_(forwardSink)
    {
    }

    /**
     * Construct a CapturingSink that forwards to a Journal's sink.
     *
     * @param journal Journal whose sink will receive forwarded messages.
     * @param thresh The minimum severity level to capture.
     */
    explicit CapturingSink(beast::Journal const& journal, Severity thresh = Severity::Trace)
        : CapturingSink(&journal.sink(), thresh)
    {
    }

    /**
     * Set or change the forward sink.
     *
     * @param sink The sink to forward messages to, or nullptr to disable.
     */
    void
    setForwardSink(beast::Journal::Sink* sink)
    {
        forwardSink_ = sink;
    }

    bool
    active(Severity level) const override
    {
        // Always capture messages at or above our threshold,
        // regardless of forward sink status
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
    write(Severity level, std::string const& text) override
    {
        if (level < threshold())
            return;
        writeAlways(level, text);
    }

    void
    writeAlways(Severity level, std::string const& text) override
    {
        {
            std::lock_guard lock(mutex_);
            entries_.push_back({level, text});
        }

        if (forwardSink_)
            forwardSink_->writeAlways(level, text);
    }

    /**
     * Get all captured log entries.
     */
    std::vector<Entry>
    getEntries() const
    {
        std::lock_guard lock(mutex_);
        return entries_;
    }

    /**
     * Clear all captured entries.
     */
    void
    clear()
    {
        std::lock_guard lock(mutex_);
        entries_.clear();
    }

    /**
     * Get the number of captured entries.
     */
    std::size_t
    size() const
    {
        std::lock_guard lock(mutex_);
        return entries_.size();
    }

    /**
     * Convert severity level to string for JSON output.
     */
    static std::string
    severityToString(Severity level)
    {
        switch (level)
        {
            case Severity::Trace:
                return "trace";
            case Severity::Debug:
                return "debug";
            case Severity::Info:
                return "info";
            case Severity::Warning:
                return "warning";
            case Severity::Error:
                return "error";
            case Severity::Fatal:
                return "fatal";
            default:
                return "unknown";
        }
    }
};

}  // namespace beast
