//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_LOGJOURNAL_H_INCLUDED
#define RIPPLE_BASICS_LOGJOURNAL_H_INCLUDED

/** Adapter that exports ripple::Log functionality as a Journal::Sink. */
class LogJournal
{
public:
    /** Convert the Journal::Severity to a LogSeverity. */
    static LogSeverity convertSeverity (Journal::Severity severity);

    //--------------------------------------------------------------------------

    /** A Journal::Sink that writes to the LogPartition with a given Key. */
    template <class Key>
    class PartitionSink : public Journal::Sink
    {
    public:
        PartitionSink ()
            : m_partition (LogPartition::get <Key> ())
        {
        }

        void write (Journal::Severity severity, std::string const& text)
        {
            // FIXME!
            LogSink::get()->write (text, convertSeverity (severity), m_partition.getName());
        }

        bool active (Journal::Severity severity)
        {
            return m_partition.doLog (convertSeverity (severity));
        }

    private:
        LogPartition const& m_partition;
    };

    //--------------------------------------------------------------------------

    /** Returns a Journal outputting through the LogPartition for the Key. */ 
    template <class Key>
    static Journal get ()
    {
        return Journal (SharedSingleton <PartitionSink <Key> >::get (
            SingletonLifetime::neverDestroyed));
    }
};

#endif
