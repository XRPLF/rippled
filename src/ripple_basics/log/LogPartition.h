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

class LogPartition
{
public:
    explicit LogPartition (const char* partitionName);

    /** Retrieve the LogPartition associated with an object.

        Each LogPartition is a singleton.
    */
    template <class Key>
    static LogPartition& get ()
    {
        struct LogPartitionType : LogPartition
        {
            LogPartitionType () : LogPartition (getPartitionName <Key> ())
            {
            }
        };

        return *SharedSingleton <LogPartitionType>::getInstance();
    }

    bool doLog (LogSeverity s) const
    {
        return s >= mMinSeverity;
    }

    const std::string& getName () const
    {
        return mName;
    }

    void setMinimumSeverity (LogSeverity severity);

    static bool setSeverity (const std::string& partition, LogSeverity severity);
    static void setSeverity (LogSeverity severity);
    static std::vector< std::pair<std::string, std::string> > getSeverities ();

private:
    /** Retrieve the name for a log partition.
    */
    template <class Key>
    static char const* getPartitionName ();

private:
    // VFALCO TODO Use an intrusive linked list
    //
    static LogPartition* headLog;

    LogPartition*       mNextLog;
    LogSeverity         mMinSeverity;
    std::string         mName;
};

#define SETUP_LOG(Class) \
    template <> char const* LogPartition::getPartitionName <Class> () { return #Class; } \
    struct Class##Instantiator { Class##Instantiator () { LogPartition::get <Class> (); } }; \
    static Class##Instantiator Class##Instantiator_instance;

#define SETUP_LOGN(Class,Name) \
    template <> char const* LogPartition::getPartitionName <Class> () { return Name; } \
    struct Class##Instantiator { Class##Instantiator () { LogPartition::get <Class> (); } }; \
    static Class##Instantiator Class##Instantiator_instance;

#endif
