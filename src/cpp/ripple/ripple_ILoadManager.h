//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_ILOADMANAGER_H
#define RIPPLE_ILOADMANAGER_H

// types of load that can be placed on the server
// VFALCO TODO replace LT_ with loadType in constants
enum LoadType
{
    // Bad things
    LT_InvalidRequest,          // A request that we can immediately tell is invalid
    LT_RequestNoReply,          // A request that we cannot satisfy
    LT_InvalidSignature,        // An object whose signature we had to check and it failed
    LT_UnwantedData,            // Data we have no use for
    LT_BadPoW,                  // Proof of work not valid
    LT_BadData,                 // Data we have to verify before rejecting

    // RPC loads
    LT_RPCInvalid,              // An RPC request that we can immediately tell is invalid.
    LT_RPCReference,            // A default "reference" unspecified load
    LT_RPCException,            // An RPC load that causes an exception
    LT_RPCBurden,               // A particularly burdensome RPC load

    // Good things
    LT_NewTrusted,              // A new transaction/validation/proposal we trust
    LT_NewTransaction,          // A new, valid transaction
    LT_NeededData,              // Data we requested

    // Requests
    LT_RequestData,             // A request that is hard to satisfy, disk access
    LT_CheapQuery,              // A query that is trivial, cached data

    LT_MAX                      // MUST BE LAST
};

//------------------------------------------------------------------------------

/** Tracks the consumption of resources at an endpoint.

    To prevent monopolization of server resources or attacks on servers,
    resource consumption is monitored at each endpoint. When consumption
    exceeds certain thresholds, costs are imposed. Costs include charging
    additional XRP for transactions, requiring a proof of work to be
    performed, or simply disconnecting the endpoint.

    Currently, consumption endpoints include websocket connections used to
    service clients, and peer connections used to create the peer to peer
    overlay network implementing the Ripple protcool.

    The current "balance" of a LoadSource represents resource consumption
    debt or credit. Debt is accrued when bad loads are imposed. Credit is
    granted when good loads are imposed. When the balance crosses heuristic
    thresholds, costs are increased on the endpoint.

    The balance is represented as a unitless relative quantity.

    @note Although RPC connections consume resources, they are transient and
          cannot be rate limited. It is advised not to expose RPC interfaces
          to the general public.
*/
class LoadSource
{
public:
    // VFALCO TODO Why even bother with a warning? Why can't we just drop?
    // VFALCO TODO Use these dispositions
    /*
    enum Disposition
    {
        none,
        shouldWarn,
        shouldDrop,
    };
    */

    /** Construct a load source.

        Sources with admin privileges have relaxed or no restrictions
        on resource consumption.

        @param admin    `true` if the source should have admin privileges.
    */
    // VFALCO TODO See who is constructing this with a parameter
    explicit LoadSource (bool admin)
        : mBalance (0)
        , mFlags (admin ? lsfPrivileged : 0)
        , mLastUpdate (UptimeTimer::getInstance ().getElapsedSeconds ())
        , mLastWarning (0)
        , mLogged (false)
    {
    }

    /** Construct a load source with a given name.

        The endpoint is considered non-privileged.
    */
    explicit LoadSource (std::string const& name)
        : mName (name)
        , mBalance (0)
        , mFlags (0)
        , mLastUpdate (UptimeTimer::getInstance ().getElapsedSeconds ())
        , mLastWarning (0)
        , mLogged (false)
    {
    }

    /** Change the name of the source.

        An endpoint can be created before it's name is known. For example,
        on an incoming connection before the IP and port have been determined.
    */
    // VFALCO TODO Figure out a way to construct the LoadSource object with
    //             the proper name instead of renaming it later.
    //
    void rename (std::string const& name)
    {
        mName = name;
    }

    /** Retrieve the name of this endpoint.
    */
    std::string const& getName () const
    {
        return mName;
    }

    /** Determine if this endpoint is privileged.
    */
    bool isPrivileged () const
    {
        return (mFlags & lsfPrivileged) != 0;
    }

    /** Grant the privileged attribute on this endpoint.
    */
    void setPrivileged ()
    {
        mFlags |= lsfPrivileged;
    }

    /** Retrieve the load debit or credit associated with the endpoint.

        The balance is represented in a unitless relative quantity
        indicating the heuristically weighted amount of resource consumption.
    */
    int getBalance () const
    {
        return mBalance;
    }

    /** Returns true if the endpoint received a log warning.
    */
    bool isLogged () const
    {
        return mLogged;
    }

    /** Reset the flag indicating the endpoint received a log warning.
    */
    void clearLogged ()
    {
        mLogged = false;
    }

    /** Indicate that this endpoint is an outgoing connection.
    */
    void setOutbound ()
    {
        mFlags |= lsfOutbound;
    }

    /** Returns true if this endpoint is an outgoing connection.
    */
    bool isOutbound () const
    {
        return (mFlags & lsfOutbound) != 0;
    }

private:
    // VFALCO Make this not a friend
    friend class LoadManager;

    static const int lsfPrivileged  = 1;
    static const int lsfOutbound    = 2;

private:
    std::string mName;
    int         mBalance;
    int         mFlags;
    int         mLastUpdate;
    int         mLastWarning;
    bool        mLogged;
};

//------------------------------------------------------------------------------

/** Manages load sources.

    This object creates an associated thread to maintain a clock.

    @see LoadSource, LoadType
*/
class ILoadManager
{
public:
    /** Create a new manager.

        @note The thresholds for warnings and punishments are in
              the ctor-initializer
    */
    static ILoadManager* New ();

    virtual ~ILoadManager () { }

    /** Start the associated thread.

        This is here to prevent the deadlock detector from activating during
        a lengthy program initialization.

        @note In stand-alone mode, this might not get called.
    */
    // VFALCO TODO Simplify the two stage initialization to one stage (construction).
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
