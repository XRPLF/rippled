//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (LoadManager)

//------------------------------------------------------------------------------

class LoadManager : public ILoadManager
{
private:
    /*  Entry mapping utilization to cost.

        The cost is expressed as a unitless relative quantity. These
        mappings are statically loaded at startup with heuristic values.
    */
    class Cost
    {
    public:
        // VFALCO TODO Eliminate this default ctor somehow
        Cost ()
            : m_loadType ()
            , m_cost (0)
            , m_resourceFlags (0)
        {
        }
        
        Cost (LoadType loadType, int cost, int resourceFlags)
            : m_loadType (loadType)
            , m_cost (cost)
            , m_resourceFlags (resourceFlags)
        {
        }

        LoadType getLoadType () const
        {
            return m_loadType;
        }

        int getCost () const
        {
            return m_cost;
        }

        int getResourceFlags () const
        {
            return m_resourceFlags;
        }

    public:
        // VFALCO TODO Make these private and const
        LoadType    m_loadType;
        int         m_cost;
        int         m_resourceFlags;
    };

public:
    LoadManager ()
        : mCreditRate (100)
        , mCreditLimit (500)
        , mDebitWarn (-500)
        , mDebitLimit (-1000)
        , mShutdown (false)
        , mArmed (false)
        , mDeadLock (0)
        , mCosts (LT_MAX)
    {
        /** Flags indicating the type of load.

            Utilization may include any combination of:

            - CPU
            - Storage space
            - Network transfer
        */
        // VFALCO NOTE These flags are not used...
        enum
        {
            flagDisk = 1,
            flagCpu  = 2,
            flagNet  = 4
        };

        // VFALCO TODO Replace this with a function that uses a simple switch statement...
        //
        addCost (Cost (LT_InvalidRequest,     -10,   flagCpu | flagNet));
        addCost (Cost (LT_RequestNoReply,      -1,   flagCpu | flagDisk));
        addCost (Cost (LT_InvalidSignature,  -100,   flagCpu));
        addCost (Cost (LT_UnwantedData,        -5,   flagCpu | flagNet));
        addCost (Cost (LT_BadData,            -20,   flagCpu));

        addCost (Cost (LT_RPCInvalid,         -10,   flagCpu | flagNet));
        addCost (Cost (LT_RPCReference,       -10,   flagCpu | flagNet));
        addCost (Cost (LT_RPCException,       -20,   flagCpu | flagNet));
        addCost (Cost (LT_RPCBurden,          -50,   flagCpu | flagNet));

        // VFALCO NOTE Why do these supposedly "good" load types still have a negative cost?
        //
        addCost (Cost (LT_NewTrusted,         -10,   0));
        addCost (Cost (LT_NewTransaction,      -2,   0));
        addCost (Cost (LT_NeededData,         -10,   0));

        addCost (Cost (LT_RequestData,         -5,   flagDisk | flagNet));
        addCost (Cost (LT_CheapQuery,          -1,   flagCpu));
    }

private:
    ~LoadManager ()
    {
        UptimeTimer::getInstance ().endManualUpdates ();

        // VFALCO TODO What is the purpose of this loop? Figure out
        //             a better way to do it.
        for (;;)
        {
            boost::this_thread::sleep (boost::posix_time::milliseconds (100));
            {
                boost::mutex::scoped_lock sl (mLock);

                if (!mShutdown)
                    return;
            }
        }
    }

    void startThread ()
    {
        UptimeTimer::getInstance ().beginManualUpdates ();

        boost::thread (boost::bind (&LoadManager::threadEntry, this)).detach ();
    }

    void canonicalize (LoadSource& source, int now) const
    {
        if (source.mLastUpdate != now)
        {
            if (source.mLastUpdate < now)
            {
                source.mBalance += mCreditRate * (now - source.mLastUpdate);

                if (source.mBalance > mCreditLimit)
                {
                    source.mBalance = mCreditLimit;
                    source.mLogged = false;
                }
            }

            source.mLastUpdate = now;
        }
    }

    bool shouldWarn (LoadSource& source) const
    {
        {
            boost::mutex::scoped_lock sl (mLock);

            int now = UptimeTimer::getInstance ().getElapsedSeconds ();
            canonicalize (source, now);

            if (source.isPrivileged () || (source.mBalance > mDebitWarn) || (source.mLastWarning == now))
                return false;

            source.mLastWarning = now;
        }
        logWarning (source.getName ());
        return true;
    }

    bool shouldCutoff (LoadSource& source) const
    {
        {
            boost::mutex::scoped_lock sl (mLock);
            int now = UptimeTimer::getInstance ().getElapsedSeconds ();
            canonicalize (source, now);

            if (source.isPrivileged () || (source.mBalance > mDebitLimit))
                return false;

            if (source.mLogged)
                return true;

            source.mLogged = true;
        }
        logDisconnect (source.getName ());
        return true;
    }

    bool applyLoadCharge (LoadSource& source, LoadType loadType) const
    {
        // FIXME: Scale by category
        Cost cost = mCosts[static_cast<int> (loadType)];
 
        return adjust (source, cost.m_cost);
    }

    bool adjust (LoadSource& source, int credits) const
    {
        // return: true = need to warn/cutoff

        // We do it this way in case we want to add exponential decay later
        int now = UptimeTimer::getInstance ().getElapsedSeconds ();
        canonicalize (source, now);
        source.mBalance += credits;

        if (source.mBalance > mCreditLimit)
            source.mBalance = mCreditLimit;

        if (source.isPrivileged ()) // privileged sources never warn/cutoff
            return false;

        if ((source.mBalance >= mDebitLimit) && (source.mLastWarning == now)) // no need to warn
            return false;

        return true;
    }

    void logWarning (const std::string& source) const
    {
        if (source.empty ())
            WriteLog (lsDEBUG, LoadManager) << "Load warning from empty source";
        else
            WriteLog (lsINFO, LoadManager) << "Load warning: " << source;
    }

    void logDisconnect (const std::string& source) const
    {
        if (source.empty ())
            WriteLog (lsINFO, LoadManager) << "Disconnect for empty source";
        else
            WriteLog (lsWARNING, LoadManager) << "Disconnect for: " << source;
    }

    // VFALCO TODO Implement this and stop accessing the vector directly
    //Cost const& getCost (LoadType loadType) const;
    int getCost (LoadType t) const
    {
        return mCosts [static_cast <int> (t)].getCost ();
    }

    void resetDeadlockDetector ()
    {
        boost::mutex::scoped_lock sl (mLock);
        mDeadLock = UptimeTimer::getInstance ().getElapsedSeconds ();
    }

    void activateDeadlockDetector ()
    {
        mArmed = true;
    }

    static void logDeadlock (int dlTime)
    {
        WriteLog (lsWARNING, LoadManager) << "Server stalled for " << dlTime << " seconds.";
    }

private:
    // VFALCO TODO These used to be public but are apparently not used. Find out why.
    /*
    int getCreditRate () const
    {
        boost::mutex::scoped_lock sl (mLock);
        return mCreditRate;
    }

    int getCreditLimit () const
    {
        boost::mutex::scoped_lock sl (mLock);
        return mCreditLimit;
    }

    int getDebitWarn () const
    {
        boost::mutex::scoped_lock sl (mLock);
        return mDebitWarn;
    }

    int getDebitLimit () const
    {
        boost::mutex::scoped_lock sl (mLock);
        return mDebitLimit;
    }

    void setCreditRate (int r)
    {
        boost::mutex::scoped_lock sl (mLock);
        mCreditRate = r;
    }

    void setCreditLimit (int r)
    {
        boost::mutex::scoped_lock sl (mLock);
        mCreditLimit = r;
    }

    void setDebitWarn (int r)
    {
        boost::mutex::scoped_lock sl (mLock);
        mDebitWarn = r;
    }

    void setDebitLimit (int r)
    {
        boost::mutex::scoped_lock sl (mLock);
        mDebitLimit = r;
    }
    */

private:
    void addCost (const Cost& c)
    {
        mCosts [static_cast <int> (c.getLoadType ())] = c;
    }

    // VFALCO NOTE Where's the thread object? It's not a data member...
    //
    void threadEntry ()
    {
        setCallingThreadName ("loadmgr");

        // VFALCO TODO replace this with a beast Time object?
        //
        // Initialize the clock to the current time.
        boost::posix_time::ptime t = boost::posix_time::microsec_clock::universal_time ();

        for (;;)
        {
            {
                // VFALCO NOTE What is this lock protecting?
                boost::mutex::scoped_lock sl (mLock);

                // Check for the shutdown flag.
                if (mShutdown)
                {
                    // VFALCO NOTE Why clear the flag now?
                    mShutdown = false;
                    return;
                }

                // VFALCO NOTE I think this is to reduce calls to the operating system
                //             for retrieving the current time.
                //
                //        TODO Instead of incrementing can't we just retrieve the current
                //             time again?
                //
                // Manually update the timer.
                UptimeTimer::getInstance ().incrementElapsedTime ();

                // Measure the amount of time we have been deadlocked, in seconds.
                //
                // VFALCO NOTE mDeadLock is a canary for detecting the condition.
                int const timeSpentDeadlocked = UptimeTimer::getInstance ().getElapsedSeconds () - mDeadLock;

                // VFALCO NOTE I think that "armed" refers to the deadlock detector
                //
                if (mArmed && (timeSpentDeadlocked >= 10))
                {
                    // Report the deadlocked condition every 10 seconds
                    if ((timeSpentDeadlocked % 10) == 0)
                    {
                        // VFALCO TODO Replace this with a dedicated thread with call queue.
                        //
                        boost::thread (BIND_TYPE (&logDeadlock, timeSpentDeadlocked)).detach ();
                    }

                    // If we go over 500 seconds spent deadlocked, it means that the
                    // deadlock resolution code has failed, which qualifies as undefined
                    // behavior.
                    //
                    assert (timeSpentDeadlocked < 500);
                }

            }

            bool change;

            // VFALCO TODO Eliminate the dependence on the Application object.
            //             Choices include constructing with the job queue / feetracker.
            //             Another option is using an observer pattern to invert the dependency.
            if (theApp->getJobQueue ().isOverloaded ())
            {
                WriteLog (lsINFO, LoadManager) << theApp->getJobQueue ().getJson (0);
                change = theApp->getFeeTrack ().raiseLocalFee ();
            }
            else
            {
                change = theApp->getFeeTrack ().lowerLocalFee ();
            }

            if (change)
            {
                // VFALCO TODO replace this with a Listener / observer and subscribe in NetworkOPs or Application
                theApp->getOPs ().reportFeeChange ();
            }

            t += boost::posix_time::seconds (1);
            boost::posix_time::time_duration when = t - boost::posix_time::microsec_clock::universal_time ();

            if ((when.is_negative ()) || (when.total_seconds () > 1))
            {
                WriteLog (lsWARNING, LoadManager) << "time jump";
                t = boost::posix_time::microsec_clock::universal_time ();
            }
            else
                boost::this_thread::sleep (when);
        }
    }

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

//------------------------------------------------------------------------------

ILoadManager* ILoadManager::New ()
{
    return new LoadManager;
}
