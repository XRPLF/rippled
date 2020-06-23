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

/** Log manager that searches for a specific message substring
 */
class CheckMessageLogs : public Logs
{
    std::string msg_;
    bool* pFound_;

    class CheckMessageSink : public beast::Journal::Sink
    {
        CheckMessageLogs& owner_;

    public:
        CheckMessageSink(
            beast::severities::Severity threshold,
            CheckMessageLogs& owner)
            : beast::Journal::Sink(threshold, false), owner_(owner)
        {
        }

        void
        write(beast::severities::Severity level, std::string const& text)
            override
        {
            if (text.find(owner_.msg_) != std::string::npos)
                *owner_.pFound_ = true;
        }
    };

public:
    /** Constructor

        @param msg The message string to search for
        @param pFound Pointer to the variable to set to true if the message is
       found
    */
    CheckMessageLogs(std::string msg, bool* pFound)
        : Logs{beast::severities::kDebug}, msg_{std::move(msg)}, pFound_{pFound}
    {
    }

    std::unique_ptr<beast::Journal::Sink>
    makeSink(
        std::string const& partition,
        beast::severities::Severity threshold) override
    {
        return std::make_unique<CheckMessageSink>(threshold, *this);
    }
};

}  // namespace test
}  // namespace ripple
