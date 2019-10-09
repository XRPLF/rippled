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

#ifndef RIPPLE_APP_MAIN_LOADMANAGER_H_INCLUDED
#define RIPPLE_APP_MAIN_LOADMANAGER_H_INCLUDED

#include <ripple/core/Stoppable.h>
#include <memory>
#include <mutex>
#include <thread>

namespace ripple {

class Application;

/** Manages load sources.

    This object creates an associated thread to maintain a clock.

    When the server is overloaded by a particular peer it issues a warning
    first. This allows friendly peers to reduce their consumption of resources,
    or disconnect from the server.

    The warning system is used instead of merely dropping, because hostile
    peers can just reconnect anyway.
*/
class LoadManager : public Stoppable
{
    LoadManager (Application& app, Stoppable& parent, beast::Journal journal);

public:
    LoadManager () = delete;
    LoadManager (LoadManager const&) = delete;
    LoadManager& operator=(LoadManager const&) = delete;

    /** Destroy the manager.

        The destructor returns only after the thread has stopped.
    */
    ~LoadManager ();

    /** Turn on deadlock detection.

        The deadlock detector begins in a disabled state. After this function
        is called, it will report deadlocks using a separate thread whenever
        the reset function is not called at least once per 10 seconds.

        @see resetDeadlockDetector
    */
    // VFALCO NOTE it seems that the deadlock detector has an "armed" state
    //             to prevent it from going off during program startup if
    //             there's a lengthy initialization operation taking place?
    //
    void activateDeadlockDetector ();

    /** Reset the deadlock detection timer.

        A dedicated thread monitors the deadlock timer, and if too much
        time passes it will produce log warnings.
    */
    void resetDeadlockDetector ();

    //--------------------------------------------------------------------------

    // Stoppable members
    void onPrepare () override;

    void onStart () override;

    void onStop () override;

private:
    void run ();

private:
    Application& app_;
    beast::Journal journal_;

    std::thread thread_;
    std::mutex mutex_;          // Guards deadLock_, armed_, and stop_.

    std::chrono::steady_clock::time_point deadLock_;  // Detect server deadlocks.
    bool armed_;
    bool stop_;

    friend
    std::unique_ptr<LoadManager>
    make_LoadManager(Application& app, Stoppable& parent, beast::Journal journal);
};

std::unique_ptr<LoadManager>
make_LoadManager (
    Application& app, Stoppable& parent, beast::Journal journal);

} // ripple

#endif
