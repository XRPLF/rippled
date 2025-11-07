#ifndef XRPL_TEST_JTX_CHECKMESSAGELOGS_H_INCLUDED
#define XRPL_TEST_JTX_CHECKMESSAGELOGS_H_INCLUDED

#include <xrpl/basics/Log.h>

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

        void
        writeAlways(beast::severities::Severity level, std::string const& text)
            override
        {
            write(level, text);
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

#endif
