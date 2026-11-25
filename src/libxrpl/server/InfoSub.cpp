#include <xrpl/server/InfoSub.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/resource/Consumer.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>

namespace xrpl {

namespace {

// Number of requested entries not already tracked in `existing`. Only these are
// charged against the cap, so re-subscribing entries a connection already holds
// is free.
template <typename T>
[[nodiscard]] std::size_t
countNew(HashSet<T> const& requested, HashSet<T> const& existing)
{
    std::size_t fresh = 0;
    for (auto const& entry : requested)
    {
        if (!existing.contains(entry))
            ++fresh;
    }
    return fresh;
}

}  // namespace

// This is the primary interface into the "client" portion of the program.
// Code that wants to do normal operations on the network such as
// creating and monitoring accounts, creating transactions, and so on
// should use this interface. The RPC code will primarily be a light wrapper
// over this code.

// Eventually, it will check the node's operating mode (synced, unsynced,
// etcetera) and defer to the correct means of processing. The current
// code assumes this node is synced (and will continue to do so until
// there's a functional network.

InfoSub::InfoSub(Source& source, Consumer consumer)
    : seq_(++nextId_), consumer_(std::move(consumer)), source_(source)
{
}

InfoSub::~InfoSub()
{
    auto const j = source_.journal();

    // Logs and swallows an exception from a single teardown step, so that an
    // exception from one cleanup step does not prevent subsequent steps from
    // running and will not escape the destructor, which would terminate the
    // process.
    auto const guarded = [&](std::string_view step, auto&& f) noexcept {
        try
        {
            f();
        }
        catch (std::exception const& e)
        {
            JLOG(j.warn()) << "~InfoSub[" << seq_ << "]: " << step << " failed: " << e.what();
        }
        catch (...)
        {
            JLOG(j.warn()) << "~InfoSub[" << seq_ << "]: " << step << " failed: unknown exception";
        }
    };

    guarded("unsubTransactions", [&] { source_.unsubTransactions(seq_); });
    guarded("unsubRTTransactions", [&] { source_.unsubRTTransactions(seq_); });
    guarded("unsubLedger", [&] { source_.unsubLedger(seq_); });
    guarded("unsubManifests", [&] { source_.unsubManifests(seq_); });
    guarded("unsubServer", [&] { source_.unsubServer(seq_); });
    guarded("unsubValidations", [&] { source_.unsubValidations(seq_); });
    guarded("unsubPeerStatus", [&] { source_.unsubPeerStatus(seq_); });
    guarded("unsubConsensus", [&] { source_.unsubConsensus(seq_); });

    // The *Internal variants erase one entry under a briefly held lock and do
    // not write back to this partially destroyed object.
    for (auto const& mptID : mptSubscriptions_)
        guarded("unsubMPTInternal", [&] { source_.unsubMPTInternal(seq_, mptID); });

    for (auto const& book : bookSubscriptions_)
        guarded("unsubBookInternal", [&] { source_.unsubBookInternal(seq_, book); });

    // Account sets can be large, so hand them off for chunked off-thread
    // removal rather than holding the account lock across the erase loop.
    // Moving them unlocked is safe: we are in the destructor, so no strong
    // reference exists and no publisher can lock the weak_ptr. If the enqueue
    // throws, the next publish prunes the dead weak_ptrs instead.
    guarded("scheduleAccountCleanup", [&] {
        source_.scheduleAccountCleanup(
            seq_,
            std::move(realTimeSubscriptions_),
            std::move(normalSubscriptions_),
            std::move(accountHistorySubscriptions_));
    });
}

resource::Consumer&
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

std::size_t
InfoSub::subscriptionCount(ScopedLock const&) const
{
    return normalSubscriptions_.size() + realTimeSubscriptions_.size() +
        accountHistorySubscriptions_.size() + mptSubscriptions_.size();
}

std::size_t
InfoSub::totalSubscriptionCount() const
{
    // Hold lock_ for the whole read so the counted sets cannot be mutated
    // mid-count by a concurrent (un)subscribe on this connection.
    std::scoped_lock const sl(lock_);

    return subscriptionCount(sl);
}

bool
InfoSub::tryReserveAccountSubscriptions(
    HashSet<AccountID> const& proposedAccounts,
    HashSet<AccountID> const& normalAccounts,
    std::size_t cap)
{
    // One lock hold covers the count, the check and the insert.
    std::scoped_lock const sl(lock_);

    std::size_t const additional = countNew(proposedAccounts, realTimeSubscriptions_) +
        countNew(normalAccounts, normalSubscriptions_);

    if (exceedsSubscriptionCap(subscriptionCount(sl), additional, cap))
        return false;

    realTimeSubscriptions_.insert(proposedAccounts.begin(), proposedAccounts.end());
    normalSubscriptions_.insert(normalAccounts.begin(), normalAccounts.end());
    return true;
}

bool
InfoSub::tryReserveMPTSubscriptions(HashSet<MPTID> const& mptIDs, std::size_t cap)
{
    // One lock hold covers the count, the check and the insert.
    std::scoped_lock const sl(lock_);

    if (exceedsSubscriptionCap(subscriptionCount(sl), countNew(mptIDs, mptSubscriptions_), cap))
        return false;

    mptSubscriptions_.insert(mptIDs.begin(), mptIDs.end());
    return true;
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

bool
InfoSub::hasAccountHistorySubscription(AccountID const& account) const
{
    std::scoped_lock const sl(lock_);
    return accountHistorySubscriptions_.contains(account);
}

void
InfoSub::insertBookSubscription(Book const& book)
{
    std::scoped_lock const sl(lock_);
    bookSubscriptions_.insert(book);
}

void
InfoSub::deleteBookSubscription(Book const& book)
{
    std::scoped_lock const sl(lock_);
    bookSubscriptions_.erase(book);
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

void
InfoSub::insertSubMPTInfo(MPTID const& mptID)
{
    std::scoped_lock const sl(lock_);

    mptSubscriptions_.insert(mptID);
}

void
InfoSub::deleteSubMPTInfo(MPTID const& mptID)
{
    std::scoped_lock const sl(lock_);

    mptSubscriptions_.erase(mptID);
}

}  // namespace xrpl
