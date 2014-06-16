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

#ifndef RIPPLE_BASICS_LOGPARTITION_H_INCLUDED
#define RIPPLE_BASICS_LOGPARTITION_H_INCLUDED

#include <ripple/basics/log/LogSeverity.h>
#include <beast/module/core/memory/SharedSingleton.h>
#include <beast/utility/Journal.h>
#include <vector>
#include <string>

namespace ripple {

class LogPartition : public beast::Journal::Sink
{
public:
    //--------------------------------------------------------------------------
    //
    // Journal::Sink
    //
    //--------------------------------------------------------------------------

    void write (beast::Journal::Severity level, std::string const& text);

    //--------------------------------------------------------------------------

    /** Construct the partition with the specified name. */
    explicit LogPartition (std::string const& name);

    /** Returns `true` output is produced at the given severity. */
    bool doLog (LogSeverity s) const;

    /** Returns the name of this partition. */
    std::string const& getName () const;

    /** Return the lowest severity reported on the partition. */
    LogSeverity getSeverity() const;

    /** Sets the lowest severity reported on the partition. */
    void setMinimumSeverity (LogSeverity severity);

    //--------------------------------------------------------------------------

    /** Returns the LogPartition based on a type key. */
    template <class Key>
    static LogPartition& get ();

    /** Returns a Journal using the specified LogPartition type key. */
    template <class Key>
    static beast::Journal getJournal ()
    {
        return beast::Journal (get <Key> ());
    }

    /** Returns a cleaned up source code file name. */
    static std::string canonicalFileName (char const* fileName);

    /** Returns the partition with the given name or nullptr if no match. */
    static LogPartition* find (std::string const& name);

    /** Set the minimum severity of all existing partitions at once. */
    static void setSeverity (LogSeverity severity);

    /** Set the minimum severity of a partition by name. */
    static bool setSeverity (std::string const& name, LogSeverity severity);

    /** Activate console output for the specified comma-separated partition list. */
    static void setConsoleOutput (std::string const& list);

    /** Returns a list of all partitions and their severity levels. */
    typedef std::vector <std::pair <std::string, std::string> > Severities;
    static Severities getSeverities ();

    /** Convert the Journal::Severity to and from a LogSeverity. */
    /** @{ */
    static LogSeverity convertSeverity (beast::Journal::Severity level);
    static beast::Journal::Severity convertLogSeverity (LogSeverity level);
    /** @} */

    /** Retrieve the name for a log partition. */
    template <class Key>
    static char const* getPartitionName ();

private:
    // VFALCO TODO Use an intrusive linked list
    //
    static LogPartition* headLog;

    LogPartition*       mNextLog;
    std::string         mName;
};

//------------------------------------------------------------------------------

namespace detail {

template <class Key>
struct LogPartitionType : LogPartition
{
    LogPartitionType () : LogPartition (getPartitionName <Key> ())
        { }
};

}

template <class Key>
LogPartition& LogPartition::get ()
{
    return *beast::SharedSingleton <
      detail::LogPartitionType <Key>>::getInstance();
}

//------------------------------------------------------------------------------

// VFALCO These macros are deprecated. Use the Journal class instead.

#define SETUP_LOG(Class) \
    template <> char const* LogPartition::getPartitionName <Class> () { return #Class; } \
    struct Class##Instantiator { Class##Instantiator () { LogPartition::get <Class> (); } }; \
    static Class##Instantiator Class##Instantiator_instance;

#define SETUP_LOGN(Class,Name) \
    template <> char const* LogPartition::getPartitionName <Class> () { return Name; } \
    struct Class##Instantiator { Class##Instantiator () { LogPartition::get <Class> (); } }; \
    static Class##Instantiator Class##Instantiator_instance;

} // ripple

#endif

