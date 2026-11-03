#pragma once

#include <xrpl/beast/utility/Journal.h>

#include <mutex>
#include <sstream>
#include <string>

namespace xrpl::test {

class CaptureSink : public beast::Journal::Sink
{
    mutable std::mutex mutex_;
    std::stringstream strm_;

public:
    explicit CaptureSink(beast::Severity threshold = beast::Severity::Debug)
        : Sink{threshold, false}
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
        // Journal sinks may be written to concurrently (e.g. from a backend's background workers),
        // so serialize access to strm_. write() funnels into writeAlways(), so the lock lives here
        // only: locking in both would self-deadlock on this non-recursive mutex.
        std::scoped_lock const lock(mutex_);
        strm_ << text << '\n';
    }

    [[nodiscard]] std::string
    messages() const
    {
        // Returns a snapshot of the captured output. Takes the lock so the read is safe even if a
        // writer is still active.
        std::scoped_lock const lock(mutex_);
        return strm_.str();
    }
};

}  // namespace xrpl::test
