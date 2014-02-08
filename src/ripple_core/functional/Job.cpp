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

namespace ripple {

Job::Job ()
    : mType (jtINVALID)
    , mJobIndex (0)
{
}

Job::Job (JobType type, uint64 index)
    : mType (type)
    , mJobIndex (index)
{
}

#if 0
Job::Job (Job const& other)
    : m_cancelCallback (other.m_cancelCallback)
    , mType (other.mType)
    , mJobIndex (other.mJobIndex)
    , mJob (other.mJob)
    , m_loadEvent (other.m_loadEvent)
    , mName (other.mName)
    , m_queue_time (other.m_queue_time)
{
}
#endif

Job::Job (JobType type,
          std::string const& name,
          uint64 index,
          LoadMonitor& lm,
          std::function <void (Job&)> const& job,
          CancelCallback cancelCallback)
    : m_cancelCallback (cancelCallback)
    , mType (type)
    , mJobIndex (index)
    , mJob (job)
    , mName (name)
    , m_queue_time (clock_type::now ())
{
    m_loadEvent = boost::make_shared <LoadEvent> (boost::ref (lm), name, false);
}

/*
Job& Job::operator= (Job const& other)
{
    mType = other.mType;
    mJobIndex = other.mJobIndex;
    mJob = other.mJob;
    m_loadEvent = other.m_loadEvent;
    mName = other.mName;
    m_cancelCallback = other.m_cancelCallback;
    return *this;
}
*/

JobType Job::getType () const
{
    return mType;
}

CancelCallback Job::getCancelCallback () const
{
    bassert (! m_cancelCallback.empty());
    return m_cancelCallback;
}

Job::clock_type::time_point const& Job::queue_time () const
{
    return m_queue_time;
}

bool Job::shouldCancel () const
{
    if (! m_cancelCallback.empty ())
        return m_cancelCallback ();
    return false;
}

void Job::doJob ()
{
    m_loadEvent->start ();
    m_loadEvent->reName (mName);

    mJob (*this);
}

void Job::rename (std::string const& newName)
{
    mName = newName;
}

const char* Job::toString (JobType t)
{
    switch (t)
    {
    case jtINVALID:         return "invalid";
    case jtPACK:            return "peerLedgerReq";
    case jtPUBOLDLEDGER:    return "publishAcqLedger";
    case jtVALIDATION_ut:   return "untrustedValidation";
    case jtPROOFWORK:       return "proofOfWork";
    case jtTRANSACTION_l:   return "localTransaction";
    case jtPROPOSAL_ut:     return "untrustedProposal";
    case jtLEDGER_DATA:     return "ledgerData";
    case jtUPDATE_PF:       return "updatePaths";
    case jtCLIENT:          return "clientCommand";
    case jtRPC:             return "RPC";
    case jtTRANSACTION:     return "transaction";
    case jtUNL:             return "unl";
    case jtADVANCE:         return "advanceLedger";
    case jtPUBLEDGER:       return "publishNewLedger";
    case jtTXN_DATA:        return "fetchTxnData";
    case jtWAL:             return "writeAhead";
    case jtVALIDATION_t:    return "trustedValidation";
    case jtWRITE:           return "writeObjects";
    case jtACCEPT:          return "acceptLedger";
    case jtPROPOSAL_t:      return "trustedProposal";
    case jtSWEEP:           return "sweep";
    case jtNETOP_CLUSTER:   return "clusterReport";
    case jtNETOP_TIMER:     return "heartbeat";

    case jtADMIN:           return "administration";

    // special types not dispatched by the job pool
    case jtPEER:            return "peerCommand";
    case jtDISK:            return "diskAccess";
    case jtTXN_PROC:        return "processTransaction";
    case jtOB_SETUP:        return "orderBookSetup";
    case jtPATH_FIND:       return "pathFind";
    case jtHO_READ:         return "nodeRead";
    case jtHO_WRITE:        return "nodeWrite";
    case jtGENERIC:         return "generic";
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

}
