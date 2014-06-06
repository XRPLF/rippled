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

#ifndef RIPPLE_PATHREQUEST_H
#define RIPPLE_PATHREQUEST_H

namespace ripple {

// A pathfinding request submitted by a client
// The request issuer must maintain a strong pointer

class RippleLineCache;
class PathRequests;

// Return values from parseJson <0 = invalid, >0 = valid
#define PFR_PJ_INVALID              -1
#define PFR_PJ_NOCHANGE             0
#define PFR_PJ_CHANGE               1

class PathRequest :
    public std::enable_shared_from_this <PathRequest>,
    public CountedObject <PathRequest>
{
public:
    static char const* getCountedObjectName () { return "PathRequest"; }

    typedef std::weak_ptr<PathRequest>    wptr;
    typedef std::shared_ptr<PathRequest>  pointer;
    typedef const pointer&                  ref;
    typedef const wptr&                     wref;
    typedef std::pair<uint160, uint160>     currIssuer_t;

public:
    // VFALCO TODO Break the cyclic dependency on InfoSub
    PathRequest (std::shared_ptr <InfoSub> const& subscriber,
        int id, PathRequests&, beast::Journal journal);

    ~PathRequest ();

    bool        isValid ();
    bool        isNew ();
    bool        needsUpdate (bool newOnly, LedgerIndex index);
    void        updateComplete ();
    Json::Value getStatus ();

    Json::Value doCreate (const std::shared_ptr<Ledger>&, const RippleLineCache::pointer&,
        const Json::Value&, bool&);
    Json::Value doClose (const Json::Value&);
    Json::Value doStatus (const Json::Value&);
    Json::Value doUpdate (const std::shared_ptr<RippleLineCache>&, bool fast); // update jvStatus
    InfoSub::pointer getSubscriber ();

private:
    bool isValid (RippleLineCache::ref crCache);
    void setValid ();
    void resetLevel (int level);
    int parseJson (const Json::Value&, bool complete);

    beast::Journal m_journal;

    typedef RippleRecursiveMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    PathRequests&                   mOwner;

    std::weak_ptr<InfoSub>        wpSubscriber;               // Who this request came from
    Json::Value                     jvId;
    Json::Value                     jvStatus;                   // Last result

    // Client request parameters
    RippleAddress                     raSrcAccount;
    RippleAddress                     raDstAccount;
    STAmount                          saDstAmount;
    std::set<currIssuer_t>            sciSourceCurrencies;
    // std::vector<Json::Value>          vjvBridges;
    std::map<currIssuer_t, STPathSet> mContext;

    bool                            bValid;

    LockType                        mIndexLock;
    LedgerIndex                     mLastIndex;
    bool                            mInProgress;

    int                             iLastLevel;
    bool                            bLastSuccess;

    int                             iIdentifier;

    boost::posix_time::ptime        ptCreated;
    boost::posix_time::ptime        ptQuickReply;
    boost::posix_time::ptime        ptFullReply;

};

} // ripple

#endif
