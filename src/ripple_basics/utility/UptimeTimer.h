//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_UPTIMETIMER_H
#define RIPPLE_UPTIMETIMER_H

/** Tracks program uptime.

    The timer can be switched to a manual system of updating, to reduce
    system calls. (?)
*/
// VFALCO TODO determine if the non-manual timing is actually needed
class UptimeTimer
{
private:
    UptimeTimer ();
    ~UptimeTimer ();

public:
    int getElapsedSeconds () const;

    void beginManualUpdates ();
    void endManualUpdates ();

    void incrementElapsedTime ();

    static UptimeTimer& getInstance ();

private:
    // VFALCO DEPRECATED, Use a memory barrier instead of forcing a cache line
    int m_pad1; // make sure m_elapsedTime fits in its own cache line
    int volatile m_elapsedTime;
    int m_pad2;

    time_t m_startTime;

    bool m_isUpdatingManually;
};

#endif
