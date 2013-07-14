//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

Job::Job ()
    : mType (jtINVALID)
    , mJobIndex (0)
    , m_limit (0)
{
}

Job::Job (JobType type, uint64 index)
    : mType (type)
    , mJobIndex (index)
    , m_limit (0)
{
}

Job::Job (JobType type,
          std::string const& name,
          int limit,
          uint64 index,
          LoadMonitor& lm,
          FUNCTION_TYPE <void (Job&)> const& job)
    : mType (type)
    , mJobIndex (index)
    , mJob (job)
    , mName (name)
    , m_limit(limit)
{
    m_loadEvent = boost::make_shared <LoadEvent> (boost::ref (lm), name, false);
}

JobType Job::getType () const
{
    return mType;
}

void Job::doJob ()
{
    m_loadEvent->start ();

    mJob (*this);

    // VFALCO TODO Isn't there a way to construct the load event with
    //             the proper name? This way the load event object
    //             can have the invariant "name is always set"
    //
    m_loadEvent->reName (mName);
}

void Job::rename (std::string const& newName)
{
    mName = newName;
}

int Job::getLimit () const
{
    return m_limit;
}

const char* Job::toString (JobType t)
{
    switch (t)
    {
    case jtINVALID:
        return "invalid";

    case jtPACK:
        return "makeFetchPack";

    case jtPUBOLDLEDGER:
        return "publishAcqLedger";

    case jtVALIDATION_ut:
        return "untrustedValidation";

    case jtPROOFWORK:
        return "proofOfWork";

    case jtPROPOSAL_ut:
        return "untrustedProposal";

    case jtLEDGER_DATA:
        return "ledgerData";

    case jtUPDATE_PF:
        return "updatePaths";

    case jtCLIENT:
        return "clientCommand";

    case jtTRANSACTION:
        return "transaction";

    case jtPUBLEDGER:
        return "publishNewLedger";

    case jtVALIDATION_t:
        return "trustedValidation";

    case jtWAL:
        return "writeAhead";

    case jtWRITE:
        return "writeObjects";

    case jtTRANSACTION_l:
        return "localTransaction";

    case jtPROPOSAL_t:
        return "trustedProposal";

    case jtADMIN:
        return "administration";

    case jtDEATH:
        return "jobOfDeath";

    case jtPEER:
        return "peerCommand";

    case jtDISK:
        return "diskAccess";

    case jtACCEPTLEDGER:
        return "acceptLedger";

    case jtTXN_PROC:
        return "processTransaction";

    case jtOB_SETUP:
        return "orderBookSetup";

    case jtPATH_FIND:
        return "pathFind";

    case jtHO_READ:
        return "nodeRead";

    case jtHO_WRITE:
        return "nodeWrite";

    default:
        assert (false);
        return "unknown";
    }
}

bool Job::operator> (const Job& j) const
{
    if (mType < j.mType)
        return true;

    if (mType > j.mType)
        return false;

    return mJobIndex > j.mJobIndex;
}

bool Job::operator>= (const Job& j) const
{
    if (mType < j.mType)
        return true;

    if (mType > j.mType)
        return false;

    return mJobIndex >= j.mJobIndex;
}

bool Job::operator< (const Job& j) const
{
    if (mType < j.mType)
        return false;

    if (mType > j.mType)
        return true;

    return mJobIndex < j.mJobIndex;
}

bool Job::operator<= (const Job& j) const
{
    if (mType < j.mType)
        return false;

    if (mType > j.mType)
        return true;

    return mJobIndex <= j.mJobIndex;
}
