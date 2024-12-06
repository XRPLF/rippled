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

#include <xrpld/net/InfoSub.h>
#include <atomic>

namespace ripple {

// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.

// Eventually, it will check the node's operating mode (synched, unsynched,
// etectera) and defer to the correct means of processing. The current
// code assumes this node is synched (and will continue to do so until
// there's a functional network.

InfoSub::InfoSub(Source& source) : m_source(source), mSeq(assign_id())
{
}

InfoSub::InfoSub(Source& source, Consumer consumer)
    : m_consumer(consumer), m_source(source), mSeq(assign_id())
{
}

InfoSub::~InfoSub()
{
    m_source.unsubTransactions(mSeq);
    m_source.unsubRTTransactions(mSeq);
    m_source.unsubLedger(mSeq);
    m_source.unsubManifests(mSeq);
    m_source.unsubServer(mSeq);
    m_source.unsubValidations(mSeq);
    m_source.unsubPeerStatus(mSeq);
    m_source.unsubConsensus(mSeq);

    // Use the internal unsubscribe so that it won't call
    // back to us and modify its own parameter
    if (!realTimeSubscriptions_.empty())
        m_source.unsubAccountInternal(mSeq, realTimeSubscriptions_, true);

    if (!normalSubscriptions_.empty())
        m_source.unsubAccountInternal(mSeq, normalSubscriptions_, false);

    for (auto const& account : accountHistorySubscriptions_)
        m_source.unsubAccountHistoryInternal(mSeq, account, false);
}

Resource::Consumer&
InfoSub::getConsumer()
{
    return m_consumer;
}

std::uint64_t
InfoSub::getSeq()
{
    return mSeq;
}

void
InfoSub::onSendEmpty()
{
}

void
InfoSub::insertSubAccountInfo(AccountID const& account, bool rt)
{
    std::lock_guard sl(mLock);

    if (rt)
        realTimeSubscriptions_.insert(account);
    else
        normalSubscriptions_.insert(account);
}

void
InfoSub::deleteSubAccountInfo(AccountID const& account, bool rt)
{
    std::lock_guard sl(mLock);

    if (rt)
        realTimeSubscriptions_.erase(account);
    else
        normalSubscriptions_.erase(account);
}

bool
InfoSub::insertSubAccountHistory(AccountID const& account)
{
    std::lock_guard sl(mLock);
    return accountHistorySubscriptions_.insert(account).second;
}

void
InfoSub::deleteSubAccountHistory(AccountID const& account)
{
    std::lock_guard sl(mLock);
    accountHistorySubscriptions_.erase(account);
}

void
InfoSub::clearRequest()
{
    request_.reset();
}

void
InfoSub::setRequest(const std::shared_ptr<InfoSubRequest>& req)
{
    request_ = req;
}

const std::shared_ptr<InfoSubRequest>&
InfoSub::getRequest()
{
    return request_;
}

void
InfoSub::setApiVersion(unsigned int apiVersion)
{
    apiVersion_ = apiVersion;
}

unsigned int
InfoSub::getApiVersion() const noexcept
{
    ASSERT(
        apiVersion_ > 0, "ripple::InfoSub::getApiVersion : valid API version");
    return apiVersion_;
}

}  // namespace ripple
