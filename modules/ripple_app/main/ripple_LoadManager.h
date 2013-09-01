//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LOADMANAGER_H_INCLUDEd
#define RIPPLE_LOADMANAGER_H_INCLUDEd

/** Manages load sources.

    This object creates an associated thread to maintain a clock.

    When the server is overloaded by a particular peer it issues a warning
    first. This allows friendly peers to reduce their consumption of resources,
    or disconnect from the server.

    The warning system is used instead of merely dropping, because hostile
    peers can just reconnect anyway.

    @see LoadSource, LoadType
*/
class LoadManager
{
public:
    /** Create a new manager.

        The manager thread begins running immediately.

        @note The thresholds for warnings and punishments are in
              the ctor-initializer
    */
    static LoadManager* New ();

    /** Destroy the manager.

        The destructor returns only after the thread has stopped.
    */
    virtual ~LoadManager () { }

    /** Start the associated thread.

        This is here to prevent the deadlock detector from activating during
        a lengthy program initialization.
    */
    // VFALCO TODO Simplify the two stage initialization to one stage (construction).
    //        NOTE In stand-alone mode the load manager thread isn't started
    virtual void startThread () = 0;

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

    /** Update an endpoint to reflect an imposed load.

        The balance of the endpoint is adjusted based on the heuristic cost
        of the indicated load.

        @return `true` if the endpoint should be warned or punished.
    */
    virtual bool applyLoadCharge (LoadSource& sourceToAdjust, LoadType loadToImpose) const = 0;

    // VFALCO TODO Eliminate these two functions and just make applyLoadCharge()
    //             return a LoadSource::Disposition
    //
    virtual bool shouldWarn (LoadSource&) const = 0;
    virtual bool shouldCutoff (LoadSource&) const = 0;
};

#endif
