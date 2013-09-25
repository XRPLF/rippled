//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_LOGPARTITION_H_INCLUDED
#define RIPPLE_BASICS_LOGPARTITION_H_INCLUDED

class LogPartition // : public List <LogPartition>::Node
{
public:
    LogPartition (const char* partitionName);

    /** Retrieve the LogPartition associated with an object.

        Each LogPartition is a singleton.
    */
    template <class Key>
    static LogPartition const& get ()
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
