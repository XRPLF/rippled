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

#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/Book.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/resource/Consumer.h>
#include <mutex>

namespace ripple {

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class InfoSubRequest
{
public:
    using pointer = std::shared_ptr<InfoSubRequest>;

    virtual ~InfoSubRequest() = default;

    virtual Json::Value
    doClose() = 0;
    virtual Json::Value
    doStatus(Json::Value const&) = 0;
};

/** Manages a client's subscription to data feeds.
 */
class InfoSub : public CountedObject<InfoSub>
{
public:
    using pointer = std::shared_ptr<InfoSub>;

    // VFALCO TODO Standardize on the names of weak / strong pointer type
    // aliases.
    using wptr = std::weak_ptr<InfoSub>;

    using ref = const std::shared_ptr<InfoSub>&;

    using Consumer = Resource::Consumer;

public:
    /** Abstracts the source of subscription data.
     */
    class Source
    {
    public:
        virtual ~Source() = default;

        // For some reason, these were originally called "rt"
        // for "real time". They actually refer to whether
        // you get transactions as they occur or once their
        // results are confirmed
        virtual void
        subAccount(
            ref ispListener,
            hash_set<AccountID> const& vnaAccountIDs,
            bool realTime) = 0;

        // for normal use, removes from InfoSub and server
        virtual void
        unsubAccount(
            ref isplistener,
            hash_set<AccountID> const& vnaAccountIDs,
            bool realTime) = 0;

        // for use during InfoSub destruction
        // Removes only from the server
        virtual void
        unsubAccountInternal(
            std::uint64_t uListener,
            hash_set<AccountID> const& vnaAccountIDs,
            bool realTime) = 0;

        /**
         * subscribe an account's new transactions and retrieve the account's
         * historical transactions
         * @return rpcSUCCESS if successful, otherwise an error code
         */
        virtual error_code_i
        subAccountHistory(ref ispListener, AccountID const& account) = 0;

        /**
         * unsubscribe an account's transactions
         * @param historyOnly if true, only stop historical transactions
         * @note once a client receives enough historical transactions,
         * it should unsubscribe with historyOnly == true to stop receiving
         * more historical transactions. It will continue to receive new
         * transactions.
         */
        virtual void
        unsubAccountHistory(
            ref ispListener,
            AccountID const& account,
            bool historyOnly) = 0;

        virtual void
        unsubAccountHistoryInternal(
            std::uint64_t uListener,
            AccountID const& account,
            bool historyOnly) = 0;

        // VFALCO TODO Document the bool return value
        virtual bool
        subLedger(ref ispListener, Json::Value& jvResult) = 0;
        virtual bool
        unsubLedger(std::uint64_t uListener) = 0;

        virtual bool
        subBookChanges(ref ispListener) = 0;
        virtual bool
        unsubBookChanges(std::uint64_t uListener) = 0;

        virtual bool
        subManifests(ref ispListener) = 0;
        virtual bool
        unsubManifests(std::uint64_t uListener) = 0;
        virtual void
        pubManifest(Manifest const&) = 0;

        virtual bool
        subServer(ref ispListener, Json::Value& jvResult, bool admin) = 0;
        virtual bool
        unsubServer(std::uint64_t uListener) = 0;

        virtual bool
        subBook(ref ispListener, Book const&) = 0;
        virtual bool
        unsubBook(std::uint64_t uListener, Book const&) = 0;

        virtual bool
        subTransactions(ref ispListener) = 0;
        virtual bool
        unsubTransactions(std::uint64_t uListener) = 0;

        virtual bool
        subRTTransactions(ref ispListener) = 0;
        virtual bool
        unsubRTTransactions(std::uint64_t uListener) = 0;

        virtual bool
        subValidations(ref ispListener) = 0;
        virtual bool
        unsubValidations(std::uint64_t uListener) = 0;

        virtual bool
        subPeerStatus(ref ispListener) = 0;
        virtual bool
        unsubPeerStatus(std::uint64_t uListener) = 0;
        virtual void
        pubPeerStatus(std::function<Json::Value(void)> const&) = 0;

        virtual bool
        subConsensus(ref ispListener) = 0;
        virtual bool
        unsubConsensus(std::uint64_t uListener) = 0;

        // VFALCO TODO Remove
        //             This was added for one particular partner, it
        //             "pushes" subscription data to a particular URL.
        //
        virtual pointer
        findRpcSub(std::string const& strUrl) = 0;
        virtual pointer
        addRpcSub(std::string const& strUrl, ref rspEntry) = 0;
        virtual bool
        tryRemoveRpcSub(std::string const& strUrl) = 0;
    };

public:
    InfoSub(Source& source);
    InfoSub(Source& source, Consumer consumer);

    virtual ~InfoSub();

    Consumer&
    getConsumer();

    virtual void
    send(Json::Value const& jvObj, bool broadcast) = 0;

    std::uint64_t
    getSeq();

    void
    onSendEmpty();

    void
    insertSubAccountInfo(AccountID const& account, bool rt);

    void
    deleteSubAccountInfo(AccountID const& account, bool rt);

    // return false if already subscribed to this account
    bool
    insertSubAccountHistory(AccountID const& account);

    void
    deleteSubAccountHistory(AccountID const& account);

    void
    clearRequest();

    void
    setRequest(const std::shared_ptr<InfoSubRequest>& req);

    std::shared_ptr<InfoSubRequest> const&
    getRequest();

protected:
    std::mutex mLock;

private:
    Consumer m_consumer;
    Source& m_source;
    hash_set<AccountID> realTimeSubscriptions_;
    hash_set<AccountID> normalSubscriptions_;
    std::shared_ptr<InfoSubRequest> request_;
    std::uint64_t mSeq;
    hash_set<AccountID> accountHistorySubscriptions_;

    static int
    assign_id()
    {
        static std::atomic<std::uint64_t> id(0);
        return ++id;
    }
};

}  // namespace ripple

#endif
