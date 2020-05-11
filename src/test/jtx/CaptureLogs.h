//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/Log.h>

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
    std::stringstream strm_;
    std::string* pResult_;

    /**
     * @brief sink for writing all log messages to a stringstream
     */
    class CaptureSink : public beast::Journal::Sink
    {
        std::stringstream& strm_;

    public:
        CaptureSink(
            beast::severities::Severity threshold,
            std::stringstream& strm)
            : beast::Journal::Sink(threshold, false), strm_(strm)
        {
        }

        void
        write(beast::severities::Severity level, std::string const& text)
            override
        {
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
        return std::make_unique<CaptureSink>(threshold, strm_);
    }
};

}  // namespace test
}  // namespace ripple
