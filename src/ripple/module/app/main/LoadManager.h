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

#ifndef RIPPLE_LOADMANAGER_H_INCLUDED
#define RIPPLE_LOADMANAGER_H_INCLUDED

namespace ripple {

/** Manages load sources.

    This object creates an associated thread to maintain a clock.

    When the server is overloaded by a particular peer it issues a warning
    first. This allows friendly peers to reduce their consumption of resources,
    or disconnect from the server.

    The warning system is used instead of merely dropping, because hostile
    peers can just reconnect anyway.

    @see LoadSource, LoadType
*/
class LoadManager : public beast::Stoppable
{
protected:
    explicit LoadManager (Stoppable& parent);

public:
    /** Create a new manager.

        @note The thresholds for warnings and punishments are in
              the ctor-initializer
    */
    static LoadManager* New (Stoppable& parent, beast::Journal journal);

    /** Destroy the manager.

        The destructor returns only after the thread has stopped.
    */
    virtual ~LoadManager () { }

    /** Turn on deadlock detection.

        The deadlock detector begins in a disabled state. After this function
        is called, it will report deadlocks using a separate thread whenever
        the reset function is not called at least once per 10 seconds.

        @see resetDeadlockDetector
    */
    // VFALCO NOTE it seems that the deadlock detector has an "armed" state to prevent it
    //             from going off during program startup if there's a lengthy initialization
    //             operation taking place?
    //
    virtual void activateDeadlockDetector () = 0;

    /** Reset the deadlock detection timer.

        A dedicated thread monitors the deadlock timer, and if too much
        time passes it will produce log warnings.
    */
    virtual void resetDeadlockDetector () = 0;
};

} // ripple

#endif
