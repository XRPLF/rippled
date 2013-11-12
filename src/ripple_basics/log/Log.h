//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_LOG_H_INCLUDED
#define RIPPLE_BASICS_LOG_H_INCLUDED

// A RAII helper for writing to the LogSink
//
class Log : public Uncopyable
{
public:
    explicit Log (LogSeverity s) : mSeverity (s)
    {
    }

    Log (LogSeverity s, LogPartition const& p)
        : mSeverity (s)
        , mPartitionName (p.getName ())
    {
    }

    ~Log ();

    template <class T>
    std::ostream& operator<< (const T& t) const
    {
        return oss << t;
    }

    std::ostringstream& ref () const
    {
        return oss;
    }

public:
    static std::string severityToString (LogSeverity);

    static LogSeverity stringToSeverity (std::string const&);

    /** Output stream for logging

        This is a convenient replacement for writing to `std::cerr`.

        Usage:

        @code

        Log::out () << "item1" << 2;

        @endcode

        It is not necessary to append a newline.
    */
    class out
    {
    public:
        out ()
        {
        }
        
        ~out ()
        {
            LogSink::get()->write (m_ss.str ());
        }

        template <class T>
        out& operator<< (T t)
        {
            m_ss << t;
            return *this;
        }

    private:
        std::stringstream m_ss;
    };

private:
    static std::string replaceFirstSecretWithAsterisks (std::string s);

    mutable std::ostringstream  oss;
    LogSeverity                 mSeverity;
    std::string                 mPartitionName;
};

// Manually test for whether we should log
//
#define ShouldLog(s, k) (LogPartition::get <k> ().doLog (s))

// Write to the log at the given severity level
//
#define WriteLog(s, k) if (!ShouldLog (s, k)) do {} while (0); else Log (s, LogPartition::get <k> ())

// Write to the log conditionally
//
#define CondLog(c, s, k) if (!ShouldLog (s, k) || !(c)) do {} while(0); else Log(s, LogPartition::get <k> ())

#endif
