#include <xrpl/server/InfoSub.h>

namespace xrpl {

// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.

// Eventually, it will check the node's operating mode (synced, unsynced,
// etcetera) and defer to the correct means of processing. The current
// code assumes this node is synced (and will continue to do so until
// there's a functional network.

InfoSub::InfoSub(Source& source) : source_(source), mSeq(assign_id())
{
}

InfoSub::InfoSub(Source& source, Consumer consumer)
    : consumer_(consumer), source_(source), mSeq(assign_id())
{
}

InfoSub::~InfoSub()
{
    source_.unsubTransactions(mSeq);
    source_.unsubRTTransactions(mSeq);
    source_.unsubLedger(mSeq);
    source_.unsubManifests(mSeq);
    source_.unsubServer(mSeq);
    source_.unsubValidations(mSeq);
    source_.unsubPeerStatus(mSeq);
    source_.unsubConsensus(mSeq);

    // Use the internal unsubscribe so that it won't call
    // back to us and modify its own parameter
    if (!realTimeSubscriptions_.empty())
        source_.unsubAccountInternal(mSeq, realTimeSubscriptions_, true);

    if (!normalSubscriptions_.empty())
        source_.unsubAccountInternal(mSeq, normalSubscriptions_, false);

    for (auto const& account : accountHistorySubscriptions_)
        source_.unsubAccountHistoryInternal(mSeq, account, false);
}

Resource::Consumer&
InfoSub::getConsumer()
{
    return consumer_;
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
InfoSub::setRequest(std::shared_ptr<InfoSubRequest> const& req)
{
    request_ = req;
}

std::shared_ptr<InfoSubRequest> const&
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
    XRPL_ASSERT(apiVersion_ > 0, "xrpl::InfoSub::getApiVersion : valid API version");
    return apiVersion_;
}

}  // namespace xrpl
