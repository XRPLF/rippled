#ifndef XRPL_TEST_JTX_CAPTURELOGS_H_INCLUDED
#define XRPL_TEST_JTX_CAPTURELOGS_H_INCLUDED

#include <xrpl/basics/Log.h>

namespace ripple {
namespace test {

/**
 * @brief Log manager for CaptureSinks. This class holds the stream
 * instance that is written to by the sinks. Upon destruction, all
 * contents of the stream are assigned to the string specified in the
 * ctor
 */
class CaptureLogs : public Logs
{
    std::mutex strmMutex_;
    std::stringstream strm_;
    std::string* pResult_;

    /**
     * @brief sink for writing all log messages to a stringstream
     */
    class CaptureSink : public beast::Journal::Sink
    {
        std::mutex& strmMutex_;
        std::stringstream& strm_;

    public:
        CaptureSink(
            beast::severities::Severity threshold,
            std::mutex& mutex,
            std::stringstream& strm)
            : beast::Journal::Sink(threshold, false)
            , strmMutex_(mutex)
            , strm_(strm)
        {
        }

        void
        write(beast::severities::Severity level, std::string const& text)
            override
        {
            std::lock_guard lock(strmMutex_);
            strm_ << text;
        }

        void
        writeAlways(beast::severities::Severity level, std::string const& text)
            override
        {
            std::lock_guard lock(strmMutex_);
            strm_ << text;
        }
    };

public:
    explicit CaptureLogs(std::string* pResult)
        : Logs(beast::severities::kInfo), pResult_(pResult)
    {
    }

    ~CaptureLogs() override
    {
        *pResult_ = strm_.str();
    }

    std::unique_ptr<beast::Journal::Sink>
    makeSink(
        std::string const& partition,
        beast::severities::Severity threshold) override
    {
        return std::make_unique<CaptureSink>(threshold, strmMutex_, strm_);
    }
};

}  // namespace test
}  // namespace ripple

#endif
