#include <xrpl/server/InfoSub.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/resource/Consumer.h>

#include <cstdint>
#include <memory>
#include <mutex>

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

InfoSub::InfoSub(Source& source) : source_(source), seq_(assignId())
{
}

InfoSub::InfoSub(Source& source, Consumer consumer)
    : consumer_(consumer), source_(source), seq_(assignId())
{
}

InfoSub::~InfoSub()
{
    source_.unsubTransactions(seq_);
    source_.unsubRTTransactions(seq_);
    source_.unsubLedger(seq_);
    source_.unsubManifests(seq_);
    source_.unsubServer(seq_);
    source_.unsubValidations(seq_);
    source_.unsubPeerStatus(seq_);
    source_.unsubConsensus(seq_);

    // Use the internal unsubscribe so that it won't call
    // back to us and modify its own parameter
    if (!realTimeSubscriptions_.empty())
        source_.unsubAccountInternal(seq_, realTimeSubscriptions_, true);

    if (!normalSubscriptions_.empty())
        source_.unsubAccountInternal(seq_, normalSubscriptions_, false);

    for (auto const& account : accountHistorySubscriptions_)
        source_.unsubAccountHistoryInternal(seq_, account, false);
}

Resource::Consumer&
InfoSub::getConsumer()
{
    return consumer_;
}

std::uint64_t
InfoSub::getSeq() const
{
    return seq_;
}

void
InfoSub::onSendEmpty()
{
}

void
InfoSub::insertSubAccountInfo(AccountID const& account, bool rt)
{
    std::scoped_lock const sl(lock_);

    if (rt)
    {
        realTimeSubscriptions_.insert(account);
    }
    else
    {
        normalSubscriptions_.insert(account);
    }
}

void
InfoSub::deleteSubAccountInfo(AccountID const& account, bool rt)
{
    std::scoped_lock const sl(lock_);

    if (rt)
    {
        realTimeSubscriptions_.erase(account);
    }
    else
    {
        normalSubscriptions_.erase(account);
    }
}

bool
InfoSub::insertSubAccountHistory(AccountID const& account)
{
    std::scoped_lock const sl(lock_);
    return accountHistorySubscriptions_.insert(account).second;
}

void
InfoSub::deleteSubAccountHistory(AccountID const& account)
{
    std::scoped_lock const sl(lock_);
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
