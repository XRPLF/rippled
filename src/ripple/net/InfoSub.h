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

#ifndef RIPPLE_NET_INFOSUB_H_INCLUDED
#define RIPPLE_NET_INFOSUB_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/json/json_value.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/resource/Consumer.h>
#include <ripple/protocol/Book.h>
#include <ripple/core/Stoppable.h>
#include <mutex>

namespace ripple {

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class PathRequest;

/** Manages a client's subscription to data feeds.
*/
class InfoSub
    : public CountedObject <InfoSub>
{
public:
    static char const* getCountedObjectName () { return "InfoSub"; }

    using pointer = std::shared_ptr<InfoSub>;

    // VFALCO TODO Standardize on the names of weak / strong pointer type aliases.
    using wptr = std::weak_ptr<InfoSub>;

    using ref = const std::shared_ptr<InfoSub>&;

    using Consumer = Resource::Consumer;

public:
    /** Abstracts the source of subscription data.
    */
    class Source : public Stoppable
    {
    protected:
        Source (char const* name, Stoppable& parent);

    public:

        // For some reason, these were originally called "rt"
        // for "real time". They actually refer to whether
        // you get transactions as they occur or once their
        // results are confirmed
        virtual void subAccount (ref ispListener,
            hash_set<AccountID> const& vnaAccountIDs,
            bool realTime) = 0;

        // for normal use, removes from InfoSub and server
        virtual void unsubAccount (ref isplistener,
            hash_set<AccountID> const& vnaAccountIDs,
            bool realTime) = 0;

        // for use during InfoSub destruction
        // Removes only from the server
        virtual void unsubAccountInternal (std::uint64_t uListener,
            hash_set<AccountID> const& vnaAccountIDs,
            bool realTime) = 0;

        // VFALCO TODO Document the bool return value
        virtual bool subLedger (ref ispListener, Json::Value& jvResult) = 0;
        virtual bool unsubLedger (std::uint64_t uListener) = 0;

        virtual bool subManifests (ref ispListener) = 0;
        virtual bool unsubManifests (std::uint64_t uListener) = 0;
        virtual void pubManifest (Manifest const&) = 0;

        virtual bool subServer (ref ispListener, Json::Value& jvResult,
            bool admin) = 0;
        virtual bool unsubServer (std::uint64_t uListener) = 0;

        virtual bool subBook (ref ispListener, Book const&) = 0;
        virtual bool unsubBook (std::uint64_t uListener, Book const&) = 0;

        virtual bool subTransactions (ref ispListener) = 0;
        virtual bool unsubTransactions (std::uint64_t uListener) = 0;

        virtual bool subRTTransactions (ref ispListener) = 0;
        virtual bool unsubRTTransactions (std::uint64_t uListener) = 0;

        virtual bool subValidations (ref ispListener) = 0;
        virtual bool unsubValidations (std::uint64_t uListener) = 0;

        virtual bool subPeerStatus (ref ispListener) = 0;
        virtual bool unsubPeerStatus (std::uint64_t uListener) = 0;
        virtual void pubPeerStatus (std::function<Json::Value(void)> const&) = 0;

        // VFALCO TODO Remove
        //             This was added for one particular partner, it
        //             "pushes" subscription data to a particular URL.
        //
        virtual pointer findRpcSub (std::string const& strUrl) = 0;
        virtual pointer addRpcSub (std::string const& strUrl, ref rspEntry) = 0;
    };

public:
    InfoSub (Source& source);
    InfoSub (Source& source, Consumer consumer);

    virtual ~InfoSub ();

    Consumer& getConsumer();

    virtual void send (Json::Value const& jvObj, bool broadcast) = 0;

    std::uint64_t getSeq ();

    void onSendEmpty ();

    void insertSubAccountInfo (
        AccountID const& account,
        bool rt);

    void deleteSubAccountInfo (
        AccountID const& account,
        bool rt);

    void clearPathRequest ();

    void setPathRequest (const std::shared_ptr<PathRequest>& req);

    std::shared_ptr <PathRequest> const& getPathRequest ();

protected:
    using LockType = std::mutex;
    using ScopedLockType = std::lock_guard <LockType>;
    LockType mLock;

private:
    Consumer                      m_consumer;
    Source&                       m_source;
    hash_set <AccountID> realTimeSubscriptions_;
    hash_set <AccountID> normalSubscriptions_;
    std::shared_ptr <PathRequest> mPathRequest;
    std::uint64_t                 mSeq;

    static
    int
    assign_id()
    {
        static std::atomic<std::uint64_t> id(0);
        return ++id;
    }
};

} // ripple

#endif
