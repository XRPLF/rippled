//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LOADMANAGER_H
#define RIPPLE_LOADMANAGER_H

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

    // Good things
    LT_NewTrusted,              // A new transaction/validation/proposal we trust
    LT_NewTransaction,          // A new, valid transaction
    LT_NeededData,              // Data we requested

    // Requests
    LT_RequestData,             // A request that is hard to satisfy, disk access
    LT_CheapQuery,              // A query that is trivial, cached data

    LT_MAX                      // MUST BE LAST
};

// load categories
// VFALCO NOTE These look like bit flags, name them accordingly
enum
{
    LC_Disk = 1,
    LC_CPU = 2,
    LC_Network = 4
};

/** Tracks the consumption of resources at an endpoint.

    To prevent monopolization of server resources or attacks on servers,
    resource consumption is monitored at each endpoint. When consumption
    exceeds certain thresholds, costs are imposed. Costs include charging
    additional XRP for transactions, requiring a proof of work to be
    performed, or simply disconnecting the endpoint.

    Currently, consumption endpoints include:

    - WebSocket connections
    - Peer connections

    @note Although RPC connections consume resources, they are transient and
          cannot be rate limited. It is advised not to expose RPC interfaces
          to the general public.
*/
class LoadSource
{
private:
    // VFALCO Make this not a friend
    friend class LoadManager;

public:
    // load source flags
    static const int lsfPrivileged  = 1;
    static const int lsfOutbound    = 2; // outbound connection

public:
    /** Construct a load source.

        Sources with admin privileges have relaxed or no restrictions
        on resource consumption.

        @param admin    `true` if the source has admin privileges.
    */
    explicit LoadSource (bool admin)
        : mBalance (0)
        , mFlags (admin ? lsfPrivileged : 0)
        , mLastUpdate (UptimeTimer::getInstance ().getElapsedSeconds ())
        , mLastWarning (0)
        , mLogged (false)
    {
    }

    explicit LoadSource (std::string const& name)
        : mName (name)
        , mBalance (0)
        , mFlags (0)
        , mLastUpdate (UptimeTimer::getInstance ().getElapsedSeconds ())
        , mLastWarning (0)
        , mLogged (false)
    {
    }

    // VFALCO TODO Figure out a way to construct the LoadSource object with
    //             the proper name instead of renaming it later.
    //
    void rename (std::string const& name)
    {
        mName = name;
    }

    std::string const& getName () const
    {
        return mName;
    }

    bool isPrivileged () const
    {
        return (mFlags & lsfPrivileged) != 0;
    }

    void setPrivileged ()
    {
        mFlags |= lsfPrivileged;
    }

    int getBalance () const
    {
        return mBalance;
    }

    bool isLogged () const
    {
        return mLogged;
    }

    void clearLogged ()
    {
        mLogged = false;
    }

    void setOutbound ()
    {
        mFlags |= lsfOutbound;
    }

    bool isOutbound () const
    {
        return (mFlags & lsfOutbound) != 0;
    }

private:
    std::string mName;
    int         mBalance;
    int         mFlags;
    int         mLastUpdate;
    int         mLastWarning;
    bool        mLogged;
};

// a collection of load sources
class LoadManager
{
public:
    LoadManager (int creditRate = 100,
                 int creditLimit = 500,
                 int debitWarn = -500,
                 int debitLimit = -1000);

    ~LoadManager ();

    void init ();

    bool shouldWarn (LoadSource&) const;

    bool shouldCutoff (LoadSource&) const;

    bool adjust (LoadSource&, int credits) const; // return value: false=balance okay, true=warn/cutoff

    bool adjust (LoadSource&, LoadType l) const;

    void logWarning (const std::string&) const;

    void logDisconnect (const std::string&) const;

    int getCost (LoadType t) const
    {
        return mCosts [static_cast <int> (t)].mCost;
    }

    void noDeadLock ();

    void arm ()
    {
        mArmed = true;
    }

private:
    // VFALCO TODO These used to be public but are apparently not used. Find out why.
    int getCreditRate () const;
    int getCreditLimit () const;
    int getDebitWarn () const;
    int getDebitLimit () const;
    void setCreditRate (int);
    void setCreditLimit (int);
    void setDebitWarn (int);
    void setDebitLimit (int);

private:
    class Cost
    {
    public:
        Cost ()
            : mType ()
            , mCost (0)
            , mCategories (0)
        {
        }
        
        Cost (LoadType typeOfLoad, int cost, int category)
            : mType (typeOfLoad)
            , mCost (cost)
            , mCategories (category)
        {
        }

    public:
        // VFALCO TODO Make these private and const
        LoadType    mType;
        int         mCost;
        int         mCategories;
    };

    void canonicalize (LoadSource&, int upTime) const;

    void addCost (const Cost& c)
    {
        mCosts [static_cast <int> (c.mType)] = c;
    }

    // VFALCO NOTE Where's the thread object? It's not a data member...
    //
    void threadEntry ();

private:
    int mCreditRate;            // credits gained/lost per second
    int mCreditLimit;           // the most credits a source can have
    int mDebitWarn;             // when a source drops below this, we warn
    int mDebitLimit;            // when a source drops below this, we cut it off (should be negative)

    bool mShutdown;
    bool mArmed;

    int mDeadLock;              // Detect server deadlocks

    mutable boost::mutex mLock; // VFALCO TODO Replace with juce::Mutex and remove the mutable attribute

    std::vector <Cost> mCosts;
};

#endif

// vim:ts=4
