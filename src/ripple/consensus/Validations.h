//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#ifndef RIPPLE_CONSENSUS_VALIDATIONS_H_INCLUDED
#define RIPPLE_CONSENSUS_VALIDATIONS_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/container/aged_container_utility.h>
#include <ripple/beast/container/aged_unordered_map.h>
#include <ripple/consensus/LedgerTrie.h>
#include <boost/optional.hpp>
#include <mutex>
#include <utility>
#include <vector>

namespace ripple {

/** Timing parameters to control validation staleness and expiration.

    @note These are protocol level parameters that should not be changed without
          careful consideration.  They are *not* implemented as static constexpr
          to allow simulation code to test alternate parameter settings.
 */
struct ValidationParms
{
    /** The number of seconds a validation remains current after its ledger's
        close time.

        This is a safety to protect against very old validations and the time
        it takes to adjust the close time accuracy window.
    */
    std::chrono::seconds validationCURRENT_WALL = std::chrono::minutes{5};

    /** Duration a validation remains current after first observed.

        The number of seconds a validation remains current after the time we
        first saw it. This provides faster recovery in very rare cases where the
        number of validations produced by the network is lower than normal
    */
    std::chrono::seconds validationCURRENT_LOCAL = std::chrono::minutes{3};

    /** Duration pre-close in which validations are acceptable.

        The number of seconds before a close time that we consider a validation
        acceptable. This protects against extreme clock errors
    */
    std::chrono::seconds validationCURRENT_EARLY = std::chrono::minutes{3};

    /** Duration a set of validations for a given ledger hash remain valid

        The number of seconds before a set of validations for a given ledger
        hash can expire.  This keeps validations for recent ledgers available
        for a reasonable interval.
    */
    std::chrono::seconds validationSET_EXPIRES = std::chrono::minutes{10};
};

/** Enforce validation increasing sequence requirement.

    Helper class for enforcing that a validation must be larger than all
    unexpired validation sequence numbers previously issued by the validator
    tracked by the instance of this class.
*/
template <class Seq>
class SeqEnforcer
{
    using time_point = std::chrono::steady_clock::time_point;
    Seq seq_{0};
    time_point when_;
public:
    /** Try advancing the largest observed validation ledger sequence

        Try setting the largest validation sequence observed, but return false
        if it violates the invariant that a validation must be larger than all
        unexpired validation sequence numbers.

        @param now The current time
        @param s The sequence number we want to validate
        @param p Validation parameters

        @return Whether the validation satisfies the invariant
    */
    bool
    operator()(time_point now, Seq s, ValidationParms const & p)
    {
        if(now > (when_ + p.validationSET_EXPIRES))
            seq_ = Seq{0};
        if(seq_ != Seq{0} && s <= seq_)
            return false;
        seq_ = s;
        when_ = now;
        return true;
    }

    Seq
    largest() const
    {
        return seq_;
    }
};
/** Whether a validation is still current

    Determines whether a validation can still be considered the current
    validation from a node based on when it was signed by that node and first
    seen by this node.

    @param p ValidationParms with timing parameters
    @param now Current time
    @param signTime When the validation was signed
    @param seenTime When the validation was first seen locally
*/
inline bool
isCurrent(
    ValidationParms const& p,
    NetClock::time_point now,
    NetClock::time_point signTime,
    NetClock::time_point seenTime)
{
    // Because this can be called on untrusted, possibly
    // malicious validations, we do our math in a way
    // that avoids any chance of overflowing or underflowing
    // the signing time.

    return (signTime > (now - p.validationCURRENT_EARLY)) &&
        (signTime < (now + p.validationCURRENT_WALL)) &&
        ((seenTime == NetClock::time_point{}) ||
         (seenTime < (now + p.validationCURRENT_LOCAL)));
}


/** Status of newly received validation
 */
enum class ValStatus {
    /// This was a new validation and was added
    current,
    /// Already had this exact same validation
    repeat,
    /// Not current or was older than current from this node
    stale,
    /// A validation was marked full but it violates increasing seq requirement
    badSeq
};

inline std::string
to_string(ValStatus m)
{
    switch (m)
    {
        case ValStatus::current:
            return "current";
        case ValStatus::repeat:
            return "repeat";
        case ValStatus::stale:
            return "stale";
        case ValStatus::badSeq:
            return "badSeq";
        default:
            return "unknown";
    }
}

/** Maintains current and recent ledger validations.

    Manages storage and queries related to validations received on the network.
    Stores the most current validation from nodes and sets of recent
    validations grouped by ledger identifier.

    Stored validations are not necessarily from trusted nodes, so clients
    and implementations should take care to use `trusted` member functions or
    check the validation's trusted status.

    This class uses a generic interface to allow adapting Validations for
    specific applications. The Adaptor template implements a set of helper
    functions and type definitions. The code stubs below outline the
    interface and type requirements.


    @warning The Adaptor::MutexType is used to manage concurrent access to
             private members of Validations but does not manage any data in the
             Adaptor instance itself.

    @code

    // Conforms to the Ledger type requirements of LedgerTrie
    struct Ledger;

    struct Validation
    {
        using NodeKey = ...;

        // Ledger ID associated with this validation
        Ledger::ID ledgerID() const;

        // Sequence number of validation's ledger (0 means no sequence number)
        Ledger::Seq seq() const

        // When the validation was signed
        NetClock::time_point signTime() const;

        // When the validation was first observed by this node
        NetClock::time_point seenTime() const;

        // Signing key of node that published the validation
        NodeKey key() const;

        // Whether the publishing node was trusted at the time the validation
        // arrived
        bool trusted() const;

        // Whether this is a full or partial validation
        bool full() const;

        implementation_specific_t
        unwrap() -> return the implementation-specific type being wrapped

        // ... implementation specific
    };

    class Adaptor
    {
        using Mutex = std::mutex;
        using Validation = Validation;
        using Ledger = Ledger;

        // Handle a newly stale validation, this should do minimal work since
        // it is called by Validations while it may be iterating Validations
        // under lock
        void onStale(Validation && );

        // Flush the remaining validations (typically done on shutdown)
        void flush(hash_map<NodeKey,Validation> && remaining);

        // Return the current network time (used to determine staleness)
        NetClock::time_point now() const;

        // Attempt to acquire a specific ledger.
        boost::optional<Ledger> acquire(Ledger::ID const & ledgerID);

        // ... implementation specific
    };
    @endcode

    @tparam Adaptor Provides type definitions and callbacks
*/
template <class Adaptor>
class Validations
{
    using Mutex = typename Adaptor::Mutex;
    using Validation = typename Adaptor::Validation;
    using Ledger = typename Adaptor::Ledger;
    using ID = typename Ledger::ID;
    using Seq = typename Ledger::Seq;
    using NodeKey = typename Validation::NodeKey;

    using WrappedValidationType = std::decay_t<
        std::result_of_t<decltype (&Validation::unwrap)(Validation)>>;

    using ScopedLock = std::lock_guard<Mutex>;

    // Manages concurrent access to members
    mutable Mutex mutex_;

    // Validations from currently listed and trusted nodes (partial and full)
    hash_map<NodeKey, Validation> current_;

    // Used to enforce the largest validation invariant for the local node
    SeqEnforcer<Seq> localSeqEnforcer_;

    // Sequence of the largest validation received from each node
    hash_map<NodeKey, SeqEnforcer<Seq>> seqEnforcers_;

    //! Validations from listed nodes, indexed by ledger id (partial and full)
    beast::aged_unordered_map<
        ID,
        hash_map<NodeKey, Validation>,
        std::chrono::steady_clock,
        beast::uhash<>>
        byLedger_;

    // Represents the ancestry of validated ledgers
    LedgerTrie<Ledger> trie_;

    // Last (validated) ledger successfully acquired. If in this map, it is
    // accounted for in the trie.
    hash_map<NodeKey, Ledger> lastLedger_;

    // Set of ledgers being acquired from the network
    hash_map<ID, hash_set<NodeKey>> acquiring_;

    // Parameters to determine validation staleness
    ValidationParms const parms_;

    // Adaptor instance
    // Is NOT managed by the mutex_ above
    Adaptor adaptor_;

private:
    // Remove support of a validated ledger
    void
    removeTrie(ScopedLock const&, NodeKey const& key, Validation const& val)
    {
        {
            auto it = acquiring_.find(val.ledgerID());
            if (it != acquiring_.end())
            {
                it->second.erase(key);
                if (it->second.empty())
                    acquiring_.erase(it);
            }
        }
        {
            auto it = lastLedger_.find(key);
            if (it != lastLedger_.end() && it->second.id() == val.ledgerID())
            {
                trie_.remove(it->second);
                lastLedger_.erase(key);
            }
        }
    }

    // Check if any pending acquire ledger requests are complete
    void
    checkAcquired(ScopedLock const& lock)
    {
        for (auto it = acquiring_.begin(); it != acquiring_.end();)
        {
            if (boost::optional<Ledger> ledger = adaptor_.acquire(it->first))
            {
                for (NodeKey const& key : it->second)
                    updateTrie(lock, key, *ledger);

                it = acquiring_.erase(it);
            }
            else
                ++it;
        }
    }

    // Update the trie to reflect a new validated ledger
    void
    updateTrie(ScopedLock const&, NodeKey const& key, Ledger ledger)
    {
        auto ins = lastLedger_.emplace(key, ledger);
        if (!ins.second)
        {
            trie_.remove(ins.first->second);
            ins.first->second = ledger;
        }
        trie_.insert(ledger);
    }

    /** Process a new validation

        Process a new trusted validation from a validator. This will be
        reflected only after the validated ledger is succesfully acquired by
        the local node. In the interim, the prior validated ledger from this
        node remains.

        @param lock Existing lock of mutex_
        @param key The master public key identifying the validating node
        @param val The trusted validation issued by the node
        @param priorID If not none, the ID of the last current validated ledger.
    */
    void
    updateTrie(
        ScopedLock const& lock,
        NodeKey const& key,
        Validation const& val,
        boost::optional<ID> priorID)
    {
        assert(val.trusted());

        // Clear any prior acquiring ledger for this node
        if (priorID)
        {
            auto it = acquiring_.find(*priorID);
            if (it != acquiring_.end())
            {
                it->second.erase(key);
                if (it->second.empty())
                    acquiring_.erase(it);
            }
        }

        checkAcquired(lock);

        if (boost::optional<Ledger> ledger = adaptor_.acquire(val.ledgerID()))
            updateTrie(lock, key, *ledger);
        else
            acquiring_[val.ledgerID()].insert(key);
    }

    /** Use the trie for a calculation

        Accessing the trie through this helper ensures acquiring validations
        are checked and any stale validations are flushed from the trie.

        @param lock Existing locked of mutex_
        @param f Invokable with signature (LedgerTrie<Ledger> &)

        @warning The invokable `f` is expected to be a simple transformation of
                 its arguments and will be called with mutex_ under lock.

    */
    template <class F>
    auto
    withTrie(ScopedLock const& lock, F&& f)
    {
        // Call current to flush any stale validations
        current(lock, [](auto){}, [](auto, auto){});
        checkAcquired(lock);
        return f(trie_);
    }

    /** Iterate current validations.

        Iterate current validations, flushing any which are stale.

        @param lock Existing lock of mutex_
        @param pre Invokable with signature (std::size_t) called prior to
                   looping.
        @param f Invokable with signature (NodeKey const &, Validations const &)
                 for each current validation.

        @note The invokable `pre` is called _prior_ to checking for staleness
              and reflects an upper-bound on the number of calls to `f.
        @warning The invokable `f` is expected to be a simple transformation of
                 its arguments and will be called with mutex_ under lock.
    */

    template <class Pre, class F>
    void
    current(ScopedLock const& lock, Pre&& pre, F&& f)
    {
        NetClock::time_point t = adaptor_.now();
        pre(current_.size());
        auto it = current_.begin();
        while (it != current_.end())
        {
            // Check for staleness
            if (!isCurrent(
                    parms_, t, it->second.signTime(), it->second.seenTime()))
            {
                removeTrie(lock, it->first, it->second);
                adaptor_.onStale(std::move(it->second));
                it = current_.erase(it);
            }
            else
            {
                auto cit = typename decltype(current_)::const_iterator{it};
                // contains a live record
                f(cit->first, cit->second);
                ++it;
            }
        }
    }

    /** Iterate the set of validations associated with a given ledger id

        @param lock Existing lock on mutex_
        @param ledgerID The identifier of the ledger
        @param pre Invokable with signature(std::size_t)
        @param f Invokable with signature (NodeKey const &, Validation const &)

        @note The invokable `pre` is called prior to iterating validations. The
              argument is the number of times `f` will be called.
        @warning The invokable f is expected to be a simple transformation of
       its arguments and will be called with mutex_ under lock.
    */
    template <class Pre, class F>
    void
    byLedger(ScopedLock const&, ID const& ledgerID, Pre&& pre, F&& f)
    {
        auto it = byLedger_.find(ledgerID);
        if (it != byLedger_.end())
        {
            // Update set time since it is being used
            byLedger_.touch(it);
            pre(it->second.size());
            for (auto const& keyVal : it->second)
                f(keyVal.first, keyVal.second);
        }
    }

public:
    /** Constructor

        @param p ValidationParms to control staleness/expiration of validaitons
        @param c Clock to use for expiring validations stored by ledger
        @param ts Parameters for constructing Adaptor instance
    */
    template <class... Ts>
    Validations(
        ValidationParms const& p,
        beast::abstract_clock<std::chrono::steady_clock>& c,
        Ts&&... ts)
        : byLedger_(c), parms_(p), adaptor_(std::forward<Ts>(ts)...)
    {
    }

    /** Return the adaptor instance
    */
    Adaptor const &
    adaptor() const
    {
        return adaptor_;
    }

    /** Return the validation timing parameters
     */
    ValidationParms const&
    parms() const
    {
        return parms_;
    }

    /** Return whether the local node can issue a validation for the given sequence
        number

        @param s The sequence number of the ledger the node wants to validate
        @return Whether the validation satisfies the invariant, updating the
                largest sequence number seen accordingly
    */
    bool
    canValidateSeq(Seq const s)
    {
        ScopedLock lock{mutex_};
        return localSeqEnforcer_(byLedger_.clock().now(), s, parms_);
    }

    /** Add a new validation

        Attempt to add a new validation.

        @param key The master key associated with this validation
        @param val The validationo to store
        @return The outcome

        @note The provided key may differ from the validations's  key()
              member if the validator is using ephemeral signing keys.
    */
    ValStatus
    add(NodeKey const& key, Validation const& val)
    {
        if (!isCurrent(parms_, adaptor_.now(), val.signTime(), val.seenTime()))
            return ValStatus::stale;

        {
            ScopedLock lock{mutex_};

            // Check that validation sequence is greater than any non-expired
            // validations sequence from that validator
            if (val.seq() != Seq{0})
            {
                auto const now = byLedger_.clock().now();
                SeqEnforcer<Seq>& enforcer = seqEnforcers_[key];
                if (!enforcer(now, val.seq(), parms_))
                    return ValStatus::badSeq;
            }

            // This validation is a repeat if we already have
            // one with the same id for this key
            auto const ret = byLedger_[val.ledgerID()].emplace(key, val);
            if (!ret.second && ret.first->second.key() == val.key())
                return ValStatus::repeat;

            auto const ins = current_.emplace(key, val);
            if (!ins.second)
            {
                // Replace existing only if this one is newer
                Validation& oldVal = ins.first->second;
                if (val.signTime() > oldVal.signTime())
                {
                    ID oldID = oldVal.ledgerID();
                    adaptor_.onStale(std::move(oldVal));
                    ins.first->second = val;
                    if (val.trusted())
                        updateTrie(lock, key, val, oldID);
                }
                else
                    return ValStatus::stale;
            }
            else if (val.trusted())
            {
                updateTrie(lock, key, val, boost::none);
            }
        }
        return ValStatus::current;
    }

    /** Expire old validation sets

        Remove validation sets that were accessed more than
        validationSET_EXPIRES ago.
    */
    void
    expire()
    {
        ScopedLock lock{mutex_};
        beast::expire(byLedger_, parms_.validationSET_EXPIRES);
    }

    Json::Value
    getJsonTrie() const
    {
        ScopedLock lock{mutex_};
        return trie_.getJson();
    }

    /** Return the sequence number and ID of the preferred working ledger

        A ledger is preferred if it has more support amongst trusted validators
        and is *not* an ancestor of the current working ledger; otherwise it
        remains the current working ledger.

        @param currLedger The local nodes current working ledger

        @return The sequence and id of the preferred working ledger,
                or Seq{0},ID{0} if no trusted validations are available to
                determine the preferred ledger.
    */
    std::pair<Seq, ID>
    getPreferred(Ledger const& currLedger)
    {
        Seq preferredSeq{0};
        ID preferredID{0};

        ScopedLock lock{mutex_};
        std::tie(preferredSeq, preferredID) = withTrie(
            lock, [](LedgerTrie<Ledger>& trie) { return trie.getPreferred(); });

        // No trusted validations to determine branch
        if (preferredSeq == Seq{0})
            return std::make_pair(preferredSeq, preferredID);

        Seq currSeq = currLedger.seq();
        ID currID = currLedger.id();

        // If we are the parent of the preferred ledger, stick with our
        // current ledger since we might be about to generate it
        if (preferredSeq == currSeq + Seq{1})
        {
            for (auto const& it : lastLedger_)
            {
                Ledger const& ledger = it.second;
                if (ledger.seq() == preferredSeq &&
                    ledger.id() == preferredID && ledger[currSeq] == currID)
                    return std::make_pair(currSeq, currID);
            }
        }

        // A ledger ahead of us is preferred regardless of whether it is
        // a descendent of our working ledger or it is on a different chain
        if (preferredSeq > currSeq)
            return std::make_pair(preferredSeq, preferredID);

        // Only switch to earlier sequence numbers if it is a different
        // chain to avoid jumping backward unnecessarily
        if (currLedger[preferredSeq] != preferredID)
            return std::make_pair(preferredSeq, preferredID);

        // Stick with current ledger
        return std::make_pair(currSeq, currID);
    }

    /** Get the ID of the preferred working ledger that exceeds a minimum valid
        ledger sequence number

        @param currLedger Current working ledger
        @param minValidSeq Minimum allowed sequence number

        @return ID Of the preferred ledger, or currLedger if the preferred ledger
                   is not valid
    */
    ID
    getPreferred(Ledger const& currLedger, Seq minValidSeq)
    {
        std::pair<Seq, ID> preferred = getPreferred(currLedger);
        if(preferred.first >= minValidSeq && preferred.second != ID{0})
            return preferred.second;
        return currLedger.id();

    }


    /** Determine the preferred last closed ledger for the next consensus round.

        Called before starting the next round of ledger consensus to determine the
        preferred working ledger. Uses the dominant peerCount ledger if no
        trusted validations are available.

        @param lcl Last closed ledger by this node
        @param minSeq Minimum allowed sequence number of the trusted preferred ledger
        @param peerCounts Map from ledger ids to count of peers with that as the
                          last closed ledger
        @return The preferred last closed ledger ID

        @note The minSeq does not apply to the peerCounts, since this function
              does not know their sequence number
    */
    ID
    getPreferredLCL(
        Ledger const & lcl,
        Seq minSeq,
        hash_map<ID, std::uint32_t> const& peerCounts)
    {
        std::pair<Seq, ID> preferred = getPreferred(lcl);

        // Trusted validations exist
        if (preferred.second != ID{0} && preferred.first > Seq{0})
            return (preferred.first >= minSeq) ? preferred.second : lcl.id();

        // Otherwise, rely on peer ledgers
        auto it = std::max_element(
            peerCounts.begin(), peerCounts.end(), [](auto& a, auto& b) {
                // Prefer larger counts, then larger ids on ties
                // (max_element expects this to return true if a < b)
                return std::tie(a.second, a.first) <
                    std::tie(b.second, b.first);
            });

        if (it != peerCounts.end())
            return it->first;
        return lcl.id();
    }

    /** Count the number of current trusted validators working on a ledger
        after the specified one.

        @param ledger The working ledger
        @param ledgerID The preferred ledger
        @return The number of current trusted validators working on a descendent
                of the preferred ledger

        @note If ledger.id() != ledgerID, only counts immediate child ledgers of
              ledgerID
    */
    std::size_t
    getNodesAfter(Ledger const& ledger, ID const& ledgerID)
    {
        ScopedLock lock{mutex_};

        // Use trie if ledger is the right one
        if (ledger.id() == ledgerID)
            return withTrie(lock, [&ledger](LedgerTrie<Ledger>& trie) {
                return trie.branchSupport(ledger) - trie.tipSupport(ledger);
            });

        // Count parent ledgers as fallback
        return std::count_if(
            lastLedger_.begin(),
            lastLedger_.end(),
            [&ledgerID](auto const& it) {
                auto const& curr = it.second;
                return curr.seq() > Seq{0} &&
                    curr[curr.seq() - Seq{1}] == ledgerID;
            });
    }

    /** Get the currently trusted full validations

        @return Vector of validations from currently trusted validators
    */
    std::vector<WrappedValidationType>
    currentTrusted()
    {
        std::vector<WrappedValidationType> ret;
        ScopedLock lock{mutex_};
        current(
            lock,
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted() && v.full())
                    ret.push_back(v.unwrap());
            });
        return ret;
    }

    /** Get the set of known public keys associated with current validations

        @return The set of of knowns keys for current listed validators
    */
    hash_set<NodeKey>
    getCurrentPublicKeys()
    {
        hash_set<NodeKey> ret;
        ScopedLock lock{mutex_};
        current(
            lock,
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeKey const& k, Validation const&) { ret.insert(k); });

        return ret;
    }

    /** Count the number of trusted full validations for the given ledger

        @param ledgerID The identifier of ledger of interest
        @return The number of trusted validations
    */
    std::size_t
    numTrustedForLedger(ID const& ledgerID)
    {
        std::size_t count = 0;
        ScopedLock lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t) {},  // nothing to reserve
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted() && v.full())
                    ++count;
            });
        return count;
    }

    /**  Get trusted full validations for a specific ledger

         @param ledgerID The identifier of ledger of interest
         @return Trusted validations associated with ledger
    */
    std::vector<WrappedValidationType>
    getTrustedForLedger(ID const& ledgerID)
    {
        std::vector<WrappedValidationType> res;
        ScopedLock lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted() && v.full())
                    res.emplace_back(v.unwrap());
            });

        return res;
    }

    /** Return the sign times of all trusted full validations

        @param ledgerID The identifier of ledger of interest
        @return Vector of times
    */
    std::vector<NetClock::time_point>
    getTrustedValidationTimes(ID const& ledgerID)
    {
        std::vector<NetClock::time_point> times;
        ScopedLock lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t numValidations) { times.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted() && v.full())
                    times.emplace_back(v.signTime());
            });
        return times;
    }

    /** Returns fees reported by trusted full validators in the given ledger

        @param ledgerID The identifier of ledger of interest
        @param baseFee The fee to report if not present in the validation
        @return Vector of fees
    */
    std::vector<std::uint32_t>
    fees(ID const& ledgerID, std::uint32_t baseFee)
    {
        std::vector<std::uint32_t> res;
        ScopedLock lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted() && v.full())
                {
                    boost::optional<std::uint32_t> loadFee = v.loadFee();
                    if (loadFee)
                        res.push_back(*loadFee);
                    else
                        res.push_back(baseFee);
                }
            });
        return res;
    }

    /** Flush all current validations
     */
    void
    flush()
    {
        hash_map<NodeKey, Validation> flushed;
        {
            ScopedLock lock{mutex_};
            for (auto it : current_)
            {
                flushed.emplace(it.first, std::move(it.second));
            }
            current_.clear();
        }

        adaptor_.flush(std::move(flushed));
    }
};

}  // namespace ripple
#endif
