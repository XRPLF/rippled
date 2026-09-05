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

// Wraps a Source teardown call so that an exception from one cleanup
// step does not prevent the subsequent steps from running. Source methods
// acquire a lock and can throw std::system_error; a throw out of ~InfoSub
// during stack unwinding would terminate the process. Failures are
// reported through the Source's Journal so they reach the configured log
// sinks; JLOG itself cannot throw, so the noexcept guarantee holds.
template <typename F>
void
safeUnsub(std::uint64_t seq, F&& f, beast::Journal j) noexcept
{
    try
    {
        f();
    }
    catch (std::exception const& e)
    {
        JLOG(j.warn()) << "~InfoSub[seq=" << seq << "]: cleanup step failed: " << e.what();
    }
    catch (...)
    {
        JLOG(j.warn()) << "~InfoSub[seq=" << seq << "]: cleanup step failed: unknown exception";
    }
}

// Number of requested entries not already tracked in `existing`. Only these are
// charged against the cap, so re-subscribing entries a connection already holds
// is free.
template <typename T>
[[nodiscard]] std::size_t
countNew(hash_set<T> const& requested, hash_set<T> const& existing)
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

InfoSub::InfoSub(Source& source) : source_(source), seq_(assignId())
{
}

InfoSub::InfoSub(Source& source, Consumer consumer)
    : consumer_(consumer), source_(source), seq_(assignId())
{
}

InfoSub::~InfoSub()
{
    // Stream unsubscribes are O(1): each erases this connection's single seq_
    // from one stream map, so they are cheap enough to run inline on the
    // disconnect thread.
    // Each Source teardown call below acquires a server-side lock and
    // can throw. Wrap each independent call so partial failure does not
    // skip the remaining teardown steps.

    auto const& j = source_.journal();

    safeUnsub(seq_, [&] { source_.unsubTransactions(seq_); }, j);
    safeUnsub(seq_, [&] { source_.unsubRTTransactions(seq_); }, j);
    safeUnsub(seq_, [&] { source_.unsubLedger(seq_); }, j);
    safeUnsub(seq_, [&] { source_.unsubManifests(seq_); }, j);
    safeUnsub(seq_, [&] { source_.unsubServer(seq_); }, j);
    safeUnsub(seq_, [&] { source_.unsubValidations(seq_); }, j);
    safeUnsub(seq_, [&] { source_.unsubPeerStatus(seq_); }, j);
    safeUnsub(seq_, [&] { source_.unsubConsensus(seq_); }, j);

    // MPT subscriptions are torn down in one bulk call keyed on seq_, so the
    // server-side lock is taken once rather than per issuance.
    if (!mptSubscriptions_.empty())
        safeUnsub(seq_, [&] { source_.unsubMPTInternal(seq_, mptSubscriptions_); }, j);

    // Book subscriptions are torn down inline here, keyed on seq_, rather than
    // through the chunked account cleanup below. The book set is not capped, so
    // it can be large; but each unsubBookInternal takes bookLock_ for a single
    // O(1) erase and releases it, so even a large set never holds a lock across
    // the whole loop - a competing book publish can interleave between erases.
    // The disconnect thread still does O(N) brief acquisitions. Use the internal
    // variant so it does not write back to bookSubscriptions_ on this
    // partially-destroyed object.
    for (auto const& book : bookSubscriptions_)
    {
        safeUnsub(seq_, [&] { source_.unsubBookInternal(seq_, book); }, j);
    }

    // Hand the account sets off (by move) to the Source for a chunked,
    // off-thread teardown keyed on seq_, instead of erasing them inline here.
    // This keeps the destructor from holding the account lock across a large
    // erase loop. The job never references this object, which is being
    // destroyed.
    //
    // Moving the sets without holding lock_ is safe: the destructor runs only
    // when the last shared_ptr to this InfoSub is released, so by the
    // shared_ptr contract no other thread holds a reference. Subscription maps
    // store weak_ptrs, so a concurrent publisher must weak_ptr::lock() first;
    // that succeeds only while a strong reference exists, which cannot overlap
    // with destruction. No other thread can observe the moved-from sets.
    //
    // Wrapped like the steps above: scheduleAccountCleanup enqueues a JobQueue
    // task, which allocates and locks and so can throw. A throw out of this
    // noexcept destructor would terminate the process. Skipping the cleanup on
    // throw is harmless: the account/rt maps hold weak_ptrs that the next
    // publish prunes once this InfoSub is gone, and any history paging job
    // self-terminates when its weak sink can no longer be locked.
    safeUnsub(
        seq_,
        [&] {
            source_.scheduleAccountCleanup(
                seq_,
                std::move(realTimeSubscriptions_),
                std::move(normalSubscriptions_),
                std::move(accountHistorySubscriptions_));
        },
        j);
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
InfoSub::subscriptionCount(scoped_lock const&) const
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
    hash_set<AccountID> const& proposedAccounts,
    hash_set<AccountID> const& normalAccounts,
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
InfoSub::tryReserveMPTSubscriptions(hash_set<MPTID> const& mptIDs, std::size_t cap)
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
