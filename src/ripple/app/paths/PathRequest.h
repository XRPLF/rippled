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

#ifndef RIPPLE_APP_PATHS_PATHREQUEST_H_INCLUDED
#define RIPPLE_APP_PATHS_PATHREQUEST_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/paths/Pathfinder.h>
#include <ripple/app/paths/RippleLineCache.h>
#include <ripple/json/json_value.h>
#include <ripple/net/InfoSub.h>
#include <ripple/protocol/types.h>
#include <boost/optional.hpp>
#include <map>
#include <mutex>
#include <set>

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

    using wptr    = std::weak_ptr<PathRequest>;
    using pointer = std::shared_ptr<PathRequest>;
    using ref     = const pointer&;
    using wref    = const wptr&;

public:
    // VFALCO TODO Break the cyclic dependency on InfoSub
    PathRequest (
        Application& app,
        std::shared_ptr <InfoSub> const& subscriber,
        int id,
        PathRequests&,
        beast::Journal journal);

    PathRequest (
        Application& app,
        std::function <void (void)> const& completion,
        int id,
        PathRequests&,
        beast::Journal journal);

    ~PathRequest ();

    bool        isNew ();
    bool        needsUpdate (bool newOnly, LedgerIndex index);
    void        updateComplete ();
    Json::Value getStatus ();

    Json::Value doCreate (
        const RippleLineCache::pointer&,
        Json::Value const&,
        bool&);
    Json::Value doClose (Json::Value const&);
    Json::Value doStatus (Json::Value const&);

    // update jvStatus
    Json::Value doUpdate (const std::shared_ptr<RippleLineCache>&, bool fast);
    InfoSub::pointer getSubscriber ();
    bool hasCompletion ();

private:
    using ScopedLockType = std::lock_guard <std::recursive_mutex>;

    bool isValid (RippleLineCache::ref crCache);
    void setValid ();
    void resetLevel (int level);

    std::unique_ptr<Pathfinder> const&
    getPathFinder(RippleLineCache::ref,
        hash_map<Currency, std::unique_ptr<Pathfinder>>&, Currency const&,
            STAmount const&, int const);

    void
    findPaths (RippleLineCache::ref, int const, Json::Value&);

    int parseJson (Json::Value const&);

    Application& app_;
    beast::Journal m_journal;

    std::recursive_mutex mLock;

    PathRequests& mOwner;

    std::weak_ptr<InfoSub> wpSubscriber; // Who this request came from
    std::function <void (void)> fCompletion;

    Json::Value jvId;
    Json::Value jvStatus;                   // Last result

    // Client request parameters
    boost::optional<AccountID> raSrcAccount;
    boost::optional<AccountID> raDstAccount;
    STAmount saDstAmount;
    boost::optional<STAmount> saSendMax;

    std::set<Issue> sciSourceCurrencies;
    std::map<Issue, STPathSet> mContext;

    bool convert_all_;

    std::recursive_mutex mIndexLock;
    LedgerIndex mLastIndex;
    bool mInProgress;

    int iLastLevel;
    bool bLastSuccess;

    int iIdentifier;

    boost::posix_time::ptime ptCreated;
    boost::posix_time::ptime ptQuickReply;
    boost::posix_time::ptime ptFullReply;

    static unsigned int const max_paths_ = 4;
};

} // ripple

#endif
