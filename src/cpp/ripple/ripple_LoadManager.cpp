//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (LoadManager)

LoadManager::LoadManager (int creditRate, int creditLimit, int debitWarn, int debitLimit)
    : mCreditRate (creditRate)
    , mCreditLimit (creditLimit)
    , mDebitWarn (debitWarn)
    , mDebitLimit (debitLimit)
    , mShutdown (false)
    , mArmed (false)
    , mDeadLock (0)
    , mCosts (LT_MAX)
{
    addCost (Cost (LT_InvalidRequest,     -10,   LC_CPU | LC_Network));
    addCost (Cost (LT_RequestNoReply,      -1,   LC_CPU | LC_Disk));
    addCost (Cost (LT_InvalidSignature,  -100,   LC_CPU));
    addCost (Cost (LT_UnwantedData,        -5,   LC_CPU | LC_Network));
    addCost (Cost (LT_BadData,            -20,   LC_CPU));

    addCost (Cost (LT_NewTrusted,         -10,   0));
    addCost (Cost (LT_NewTransaction,      -2,   0));
    addCost (Cost (LT_NeededData,         -10,   0));

    addCost (Cost (LT_RequestData,         -5,   LC_Disk | LC_Network));
    addCost (Cost (LT_CheapQuery,          -1,   LC_CPU));
}

void LoadManager::init ()
{
    UptimeTimer::getInstance ().beginManualUpdates ();

    boost::thread (boost::bind (&LoadManager::threadEntry, this)).detach ();
}

LoadManager::~LoadManager ()
{
    UptimeTimer::getInstance ().endManualUpdates ();

    // VFALCO What is this loop? it doesn't seem to do anything useful.
    do
    {
        boost::this_thread::sleep (boost::posix_time::milliseconds (100));
        {
            boost::mutex::scoped_lock sl (mLock);

            if (!mShutdown)
                return;
        }
    }
    while (1);
}

void LoadManager::noDeadLock ()
{
    boost::mutex::scoped_lock sl (mLock);
    //mDeadLock = mUptime;
    mDeadLock = UptimeTimer::getInstance ().getElapsedSeconds ();
}

int LoadManager::getCreditRate () const
{
    boost::mutex::scoped_lock sl (mLock);
    return mCreditRate;
}

int LoadManager::getCreditLimit () const
{
    boost::mutex::scoped_lock sl (mLock);
    return mCreditLimit;
}

int LoadManager::getDebitWarn () const
{
    boost::mutex::scoped_lock sl (mLock);
    return mDebitWarn;
}

int LoadManager::getDebitLimit () const
{
    boost::mutex::scoped_lock sl (mLock);
    return mDebitLimit;
}

void LoadManager::setCreditRate (int r)
{
    boost::mutex::scoped_lock sl (mLock);
    mCreditRate = r;
}

void LoadManager::setCreditLimit (int r)
{
    boost::mutex::scoped_lock sl (mLock);
    mCreditLimit = r;
}

void LoadManager::setDebitWarn (int r)
{
    boost::mutex::scoped_lock sl (mLock);
    mDebitWarn = r;
}

void LoadManager::setDebitLimit (int r)
{
    boost::mutex::scoped_lock sl (mLock);
    mDebitLimit = r;
}

void LoadManager::canonicalize (LoadSource& source, int now) const
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

bool LoadManager::shouldWarn (LoadSource& source) const
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

bool LoadManager::shouldCutoff (LoadSource& source) const
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

bool LoadManager::adjust (LoadSource& source, LoadType t) const
{
    // FIXME: Scale by category
    Cost cost = mCosts[static_cast<int> (t)];
    return adjust (source, cost.mCost);
}

bool LoadManager::adjust (LoadSource& source, int credits) const
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

static void LogDeadLock (int dlTime)
{
    WriteLog (lsWARNING, LoadManager) << "Server stalled for " << dlTime << " seconds.";
}

void LoadManager::threadEntry ()
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

            if (mArmed && (timeSpentDeadlocked >= 10))
            {
                // Report the deadlocked condition every 10 seconds
                if ((timeSpentDeadlocked % 10) == 0)
                {
                    // VFALCO TODO Replace this with a dedicated thread with call queue.
                    //
                    boost::thread (BIND_TYPE (&LogDeadLock, timeSpentDeadlocked)).detach ();
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

void LoadManager::logWarning (const std::string& source) const
{
    if (source.empty ())
        WriteLog (lsDEBUG, LoadManager) << "Load warning from empty source";
    else
        WriteLog (lsINFO, LoadManager) << "Load warning: " << source;
}

void LoadManager::logDisconnect (const std::string& source) const
{
    if (source.empty ())
        WriteLog (lsINFO, LoadManager) << "Disconnect for empty source";
    else
        WriteLog (lsWARNING, LoadManager) << "Disconnect for: " << source;
}

// vim:ts=4
