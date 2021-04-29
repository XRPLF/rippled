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
#include <ripple/protocol/PublicKey.h>
#include <mutex>
#include <optional>
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
    explicit ValidationParms() = default;

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

    /** How long we consider a validation fresh.
     *
     *  The number of seconds since a validation has been seen for it to
     *  be considered to accurately represent a live proposer's most recent
     *  validation. This value should be sufficiently higher than
     *  ledgerMAX_CONSENSUS such that validators who are waiting for
     *  laggards are not considered offline.
     */
    std::chrono::seconds validationFRESHNESS = std::chrono::seconds{20};
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
    operator()(time_point now, Seq s, ValidationParms const& p)
    {
        if (now > (when_ + p.validationSET_EXPIRES))
            seq_ = Seq{0};
        if (s <= seq_)
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
    // the signing time.  All of the expressions below are
    // promoted from unsigned 32 bit to signed 64 bit prior
    // to computation.

    return (signTime > (now - p.validationCURRENT_EARLY)) &&
        (signTime < (now + p.validationCURRENT_WALL)) &&
        ((seenTime == NetClock::time_point{}) ||
         (seenTime < (now + p.validationCURRENT_LOCAL)));
}

/** Status of validation we received */
enum class ValStatus {
    /// This was a new validation and was added
    current,
    /// Not current or was older than current from this node
    stale,
    /// A validation violates the increasing seq requirement
    badSeq,
    /// Multiple validations by a validator for the same ledger
    multiple,
    /// Multiple validations by a validator for different ledgers
    conflicting
};

inline std::string
to_string(ValStatus m)
{
    switch (m)
    {
        case ValStatus::current:
            return "current";
        case ValStatus::stale:
            return "stale";
        case ValStatus::badSeq:
            return "badSeq";
        case ValStatus::multiple:
            return "multiple";
        case ValStatus::conflicting:
            return "conflicting";
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
        using NodeID = ...;
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

        // Set the validation as trusted
        void setTrusted();

        // Set the validation as untrusted
        void setUntrusted();

        // Whether this is a full or partial validation
        bool full() const;

        // Identifier for this node that remains fixed even when rotating
        // signing keys
        NodeID nodeID()  const;

        implementation_specific_t
        unwrap() -> return the implementation-specific type being wrapped

        // ... implementation specific
    };

    class Adaptor
    {
        using Mutex = std::mutex;
        using Validation = Validation;
        using Ledger = Ledger;

        // Return the current network time (used to determine staleness)
        NetClock::time_point now() const;

        // Attempt to acquire a specific ledger.
        std::optional<Ledger> acquire(Ledger::ID const & ledgerID);

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
    using NodeID = typename Validation::NodeID;
    using NodeKey = typename Validation::NodeKey;

    using WrappedValidationType = std::decay_t<
        std::result_of_t<decltype (&Validation::unwrap)(Validation)>>;

    // Manages concurrent access to members
    mutable Mutex mutex_;

    // Validations from currently listed and trusted nodes (partial and full)
    hash_map<NodeID, Validation> current_;

    // Used to enforce the largest validation invariant for the local node
    SeqEnforcer<Seq> localSeqEnforcer_;

    // Sequence of the largest validation received from each node
    hash_map<NodeID, SeqEnforcer<Seq>> seqEnforcers_;

    //! Validations from listed nodes, indexed by ledger id (partial and full)
    beast::aged_unordered_map<
        ID,
        hash_map<NodeID, Validation>,
        std::chrono::steady_clock,
        beast::uhash<>>
        byLedger_;

    // Partial and full validations indexed by sequence
    beast::aged_unordered_map<
        Seq,
        hash_map<NodeID, Validation>,
        std::chrono::steady_clock,
        beast::uhash<>>
        bySequence_;

    // A range [low_, high_) of validations to keep from expire
    struct KeepRange
    {
        Seq low_;
        Seq high_;
    };
    std::optional<KeepRange> toKeep_;

    // Represents the ancestry of validated ledgers
    LedgerTrie<Ledger> trie_;

    // Last (validated) ledger successfully acquired. If in this map, it is
    // accounted for in the trie.
    hash_map<NodeID, Ledger> lastLedger_;

    // Set of ledgers being acquired from the network
    hash_map<std::pair<Seq, ID>, hash_set<NodeID>> acquiring_;

    // Parameters to determine validation staleness
    ValidationParms const parms_;

    // Adaptor instance
    // Is NOT managed by the mutex_ above
    Adaptor adaptor_;

private:
    // Remove support of a validated ledger
    void
    removeTrie(
        std::lock_guard<Mutex> const&,
        NodeID const& nodeID,
        Validation const& val)
    {
        {
            auto it =
                acquiring_.find(std::make_pair(val.seq(), val.ledgerID()));
            if (it != acquiring_.end())
            {
                it->second.erase(nodeID);
                if (it->second.empty())
                    acquiring_.erase(it);
            }
        }
        {
            auto it = lastLedger_.find(nodeID);
            if (it != lastLedger_.end() && it->second.id() == val.ledgerID())
            {
                trie_.remove(it->second);
                lastLedger_.erase(nodeID);
            }
        }
    }

    // Check if any pending acquire ledger requests are complete
    void
    checkAcquired(std::lock_guard<Mutex> const& lock)
    {
        for (auto it = acquiring_.begin(); it != acquiring_.end();)
        {
            if (std::optional<Ledger> ledger =
                    adaptor_.acquire(it->first.second))
            {
                for (NodeID const& nodeID : it->second)
                    updateTrie(lock, nodeID, *ledger);

                it = acquiring_.erase(it);
            }
            else
                ++it;
        }
    }

    // Update the trie to reflect a new validated ledger
    void
    updateTrie(
        std::lock_guard<Mutex> const&,
        NodeID const& nodeID,
        Ledger ledger)
    {
        auto const [it, inserted] = lastLedger_.emplace(nodeID, ledger);
        if (!inserted)
        {
            trie_.remove(it->second);
            it->second = ledger;
        }
        trie_.insert(ledger);
    }

    /** Process a new validation

        Process a new trusted validation from a validator. This will be
        reflected only after the validated ledger is successfully acquired by
        the local node. In the interim, the prior validated ledger from this
        node remains.

        @param lock Existing lock of mutex_
        @param nodeID The node identifier of the validating node
        @param val The trusted validation issued by the node
        @param prior If not none, the last current validated ledger Seq,ID of
                     key
    */
    void
    updateTrie(
        std::lock_guard<Mutex> const& lock,
        NodeID const& nodeID,
        Validation const& val,
        std::optional<std::pair<Seq, ID>> prior)
    {
        assert(val.trusted());

        // Clear any prior acquiring ledger for this node
        if (prior)
        {
            auto it = acquiring_.find(*prior);
            if (it != acquiring_.end())
            {
                it->second.erase(nodeID);
                if (it->second.empty())
                    acquiring_.erase(it);
            }
        }

        checkAcquired(lock);

        std::pair<Seq, ID> valPair{val.seq(), val.ledgerID()};
        auto it = acquiring_.find(valPair);
        if (it != acquiring_.end())
        {
            it->second.insert(nodeID);
        }
        else
        {
            if (std::optional<Ledger> ledger = adaptor_.acquire(val.ledgerID()))
                updateTrie(lock, nodeID, *ledger);
            else
                acquiring_[valPair].insert(nodeID);
        }
    }

    /** Use the trie for a calculation

        Accessing the trie through this helper ensures acquiring validations
        are checked and any stale validations are flushed from the trie.

        @param lock Existing lock of mutex_
        @param f Invokable with signature (LedgerTrie<Ledger> &)

        @warning The invokable `f` is expected to be a simple transformation of
                 its arguments and will be called with mutex_ under lock.

    */
    template <class F>
    auto
    withTrie(std::lock_guard<Mutex> const& lock, F&& f)
    {
        // Call current to flush any stale validations
        current(
            lock, [](auto) {}, [](auto, auto) {});
        checkAcquired(lock);
        return f(trie_);
    }

    /** Iterate current validations.

        Iterate current validations, flushing any which are stale.

        @param lock Existing lock of mutex_
        @param pre Invokable with signature (std::size_t) called prior to
                   looping.
        @param f Invokable with signature (NodeID const &, Validations const &)
                 for each current validation.

        @note The invokable `pre` is called _prior_ to checking for staleness
              and reflects an upper-bound on the number of calls to `f.
        @warning The invokable `f` is expected to be a simple transformation of
                 its arguments and will be called with mutex_ under lock.
    */

    template <class Pre, class F>
    void
    current(std::lock_guard<Mutex> const& lock, Pre&& pre, F&& f)
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
        @param f Invokable with signature (NodeID const &, Validation const &)

        @note The invokable `pre` is called prior to iterating validations. The
              argument is the number of times `f` will be called.
        @warning The invokable f is expected to be a simple transformation of
       its arguments and will be called with mutex_ under lock.
    */
    template <class Pre, class F>
    void
    byLedger(
        std::lock_guard<Mutex> const&,
        ID const& ledgerID,
        Pre&& pre,
        F&& f)
    {
        auto it = byLedger_.find(ledgerID);
        if (it != byLedger_.end())
        {
            // Update set time since it is being used
            byLedger_.touch(it);
            pre(it->second.size());
            for (auto const& [key, val] : it->second)
                f(key, val);
        }
    }

public:
    /** Constructor

        @param p ValidationParms to control staleness/expiration of validations
        @param c Clock to use for expiring validations stored by ledger
        @param ts Parameters for constructing Adaptor instance
    */
    template <class... Ts>
    Validations(
        ValidationParms const& p,
        beast::abstract_clock<std::chrono::steady_clock>& c,
        Ts&&... ts)
        : byLedger_(c)
        , bySequence_(c)
        , parms_(p)
        , adaptor_(std::forward<Ts>(ts)...)
    {
    }

    /** Return the adaptor instance
     */
    Adaptor const&
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

    /** Return whether the local node can issue a validation for the given
       sequence number

        @param s The sequence number of the ledger the node wants to validate
        @return Whether the validation satisfies the invariant, updating the
                largest sequence number seen accordingly
    */
    bool
    canValidateSeq(Seq const s)
    {
        std::lock_guard lock{mutex_};
        return localSeqEnforcer_(byLedger_.clock().now(), s, parms_);
    }

    /** Add a new validation

        Attempt to add a new validation.

        @param nodeID The identity of the node issuing this validation
        @param val The validation to store
        @return The outcome
    */
    ValStatus
    add(NodeID const& nodeID, Validation const& val)
    {
        if (!isCurrent(parms_, adaptor_.now(), val.signTime(), val.seenTime()))
            return ValStatus::stale;

        {
            std::lock_guard lock{mutex_};

            // Check that validation sequence is greater than any non-expired
            // validations sequence from that validator; if it's not, perform
            // additional work to detect Byzantine validations
            auto const now = byLedger_.clock().now();

            auto const [seqit, seqinserted] =
                bySequence_[val.seq()].emplace(nodeID, val);

            if (!seqinserted)
            {
                // Check if the entry we're already tracking was signed
                // long enough ago that we can disregard it.
                auto const diff =
                    std::max(seqit->second.signTime(), val.signTime()) -
                    std::min(seqit->second.signTime(), val.signTime());

                if (diff > parms_.validationCURRENT_WALL &&
                    val.signTime() > seqit->second.signTime())
                    seqit->second = val;
            }

            // Enforce monotonically increasing sequences for validations
            // by a given node, and run the active Byzantine detector:
            if (auto& enf = seqEnforcers_[nodeID]; !enf(now, val.seq(), parms_))
            {
                // If the validation is for the same sequence as one we are
                // tracking, check it closely:
                if (seqit->second.seq() == val.seq())
                {
                    // Two validations for the same sequence but for different
                    // ledgers. This could be the result of misconfiguration
                    // but it can also mean a Byzantine validator.
                    if (seqit->second.ledgerID() != val.ledgerID())
                        return ValStatus::conflicting;

                    // Two validations for the same sequence and for the same
                    // ledger with different sign times. This could be the
                    // result of a misconfiguration but it can also mean a
                    // Byzantine validator.
                    if (seqit->second.signTime() != val.signTime())
                        return ValStatus::conflicting;

                    // Two validations for the same sequence but with different
                    // cookies. This is probably accidental misconfiguration.
                    if (seqit->second.cookie() != val.cookie())
                        return ValStatus::multiple;
                }

                return ValStatus::badSeq;
            }

            byLedger_[val.ledgerID()].insert_or_assign(nodeID, val);

            auto const [it, inserted] = current_.emplace(nodeID, val);
            if (!inserted)
            {
                // Replace existing only if this one is newer
                Validation& oldVal = it->second;
                if (val.signTime() > oldVal.signTime())
                {
                    std::pair<Seq, ID> old(oldVal.seq(), oldVal.ledgerID());
                    it->second = val;
                    if (val.trusted())
                        updateTrie(lock, nodeID, val, old);
                }
                else
                    return ValStatus::stale;
            }
            else if (val.trusted())
            {
                updateTrie(lock, nodeID, val, std::nullopt);
            }
        }

        return ValStatus::current;
    }

    /**
     * Set the range [low, high) of validations to keep from expire
     * @param low the lower sequence number
     * @param high the higher sequence number
     * @note high must be greater than low
     */
    void
    setSeqToKeep(Seq const& low, Seq const& high)
    {
        std::lock_guard lock{mutex_};
        assert(low < high);
        toKeep_ = {low, high};
    }

    /** Expire old validation sets

        Remove validation sets that were accessed more than
        validationSET_EXPIRES ago and were not asked to keep.
    */
    void
    expire()
    {
        std::lock_guard lock{mutex_};
        if (toKeep_)
        {
            for (auto i = byLedger_.begin(); i != byLedger_.end(); ++i)
            {
                auto const& validationMap = i->second;
                if (!validationMap.empty() &&
                    validationMap.begin()->second.seq() >= toKeep_->low_ &&
                    validationMap.begin()->second.seq() < toKeep_->high_)
                {
                    byLedger_.touch(i);
                }
            }

            for (auto i = bySequence_.begin(); i != bySequence_.end(); ++i)
            {
                if (i->first >= toKeep_->low_ && i->first < toKeep_->high_)
                {
                    bySequence_.touch(i);
                }
            }
        }

        beast::expire(byLedger_, parms_.validationSET_EXPIRES);
        beast::expire(bySequence_, parms_.validationSET_EXPIRES);
    }

    /** Update trust status of validations

        Updates the trusted status of known validations to account for nodes
        that have been added or removed from the UNL. This also updates the trie
        to ensure only currently trusted nodes' validations are used.

        @param added Identifiers of nodes that are now trusted
        @param removed Identifiers of nodes that are no longer trusted
    */
    void
    trustChanged(hash_set<NodeID> const& added, hash_set<NodeID> const& removed)
    {
        std::lock_guard lock{mutex_};

        for (auto& [nodeId, validation] : current_)
        {
            if (added.find(nodeId) != added.end())
            {
                validation.setTrusted();
                updateTrie(lock, nodeId, validation, std::nullopt);
            }
            else if (removed.find(nodeId) != removed.end())
            {
                validation.setUntrusted();
                removeTrie(lock, nodeId, validation);
            }
        }

        for (auto& [_, validationMap] : byLedger_)
        {
            (void)_;
            for (auto& [nodeId, validation] : validationMap)
            {
                if (added.find(nodeId) != added.end())
                {
                    validation.setTrusted();
                }
                else if (removed.find(nodeId) != removed.end())
                {
                    validation.setUntrusted();
                }
            }
        }
    }

    Json::Value
    getJsonTrie() const
    {
        std::lock_guard lock{mutex_};
        return trie_.getJson();
    }

    /** Return the sequence number and ID of the preferred working ledger

        A ledger is preferred if it has more support amongst trusted validators
        and is *not* an ancestor of the current working ledger; otherwise it
        remains the current working ledger.

        @param curr The local node's current working ledger

        @return The sequence and id of the preferred working ledger,
                or std::nullopt if no trusted validations are available to
                determine the preferred ledger.
    */
    std::optional<std::pair<Seq, ID>>
    getPreferred(Ledger const& curr)
    {
        std::lock_guard lock{mutex_};
        std::optional<SpanTip<Ledger>> preferred =
            withTrie(lock, [this](LedgerTrie<Ledger>& trie) {
                return trie.getPreferred(localSeqEnforcer_.largest());
            });
        // No trusted validations to determine branch
        if (!preferred)
        {
            // fall back to majority over acquiring ledgers
            auto it = std::max_element(
                acquiring_.begin(),
                acquiring_.end(),
                [](auto const& a, auto const& b) {
                    std::pair<Seq, ID> const& aKey = a.first;
                    typename hash_set<NodeID>::size_type const& aSize =
                        a.second.size();
                    std::pair<Seq, ID> const& bKey = b.first;
                    typename hash_set<NodeID>::size_type const& bSize =
                        b.second.size();
                    // order by number of trusted peers validating that ledger
                    // break ties with ledger ID
                    return std::tie(aSize, aKey.second) <
                        std::tie(bSize, bKey.second);
                });
            if (it != acquiring_.end())
                return it->first;
            return std::nullopt;
        }

        // If we are the parent of the preferred ledger, stick with our
        // current ledger since we might be about to generate it
        if (preferred->seq == curr.seq() + Seq{1} &&
            preferred->ancestor(curr.seq()) == curr.id())
            return std::make_pair(curr.seq(), curr.id());

        // A ledger ahead of us is preferred regardless of whether it is
        // a descendant of our working ledger or it is on a different chain
        if (preferred->seq > curr.seq())
            return std::make_pair(preferred->seq, preferred->id);

        // Only switch to earlier or same sequence number
        // if it is a different chain.
        if (curr[preferred->seq] != preferred->id)
            return std::make_pair(preferred->seq, preferred->id);

        // Stick with current ledger
        return std::make_pair(curr.seq(), curr.id());
    }

    /** Get the ID of the preferred working ledger that exceeds a minimum valid
        ledger sequence number

        @param curr Current working ledger
        @param minValidSeq Minimum allowed sequence number

        @return ID Of the preferred ledger, or curr if the preferred ledger
                   is not valid
    */
    ID
    getPreferred(Ledger const& curr, Seq minValidSeq)
    {
        std::optional<std::pair<Seq, ID>> preferred = getPreferred(curr);
        if (preferred && preferred->first >= minValidSeq)
            return preferred->second;
        return curr.id();
    }

    /** Determine the preferred last closed ledger for the next consensus round.

        Called before starting the next round of ledger consensus to determine
        the preferred working ledger. Uses the dominant peerCount ledger if no
        trusted validations are available.

        @param lcl Last closed ledger by this node
        @param minSeq Minimum allowed sequence number of the trusted preferred
                      ledger
        @param peerCounts Map from ledger ids to count of peers with that as the
                          last closed ledger
        @return The preferred last closed ledger ID

        @note The minSeq does not apply to the peerCounts, since this function
              does not know their sequence number
    */
    ID
    getPreferredLCL(
        Ledger const& lcl,
        Seq minSeq,
        hash_map<ID, std::uint32_t> const& peerCounts)
    {
        std::optional<std::pair<Seq, ID>> preferred = getPreferred(lcl);

        // Trusted validations exist, but stick with local preferred ledger if
        // preferred is in the past
        if (preferred)
            return (preferred->first >= minSeq) ? preferred->second : lcl.id();

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
        @return The number of current trusted validators working on a descendant
                of the preferred ledger

        @note If ledger.id() != ledgerID, only counts immediate child ledgers of
              ledgerID
    */
    std::size_t
    getNodesAfter(Ledger const& ledger, ID const& ledgerID)
    {
        std::lock_guard lock{mutex_};

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
        std::lock_guard lock{mutex_};
        current(
            lock,
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeID const&, Validation const& v) {
                if (v.trusted() && v.full())
                    ret.push_back(v.unwrap());
            });
        return ret;
    }

    /** Get the set of node ids associated with current validations

        @return The set of node ids for active, listed validators
    */
    auto
    getCurrentNodeIDs() -> hash_set<NodeID>
    {
        hash_set<NodeID> ret;
        std::lock_guard lock{mutex_};
        current(
            lock,
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeID const& nid, Validation const&) { ret.insert(nid); });

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
        std::lock_guard lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t) {},  // nothing to reserve
            [&](NodeID const&, Validation const& v) {
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
        std::lock_guard lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeID const&, Validation const& v) {
                if (v.trusted() && v.full())
                    res.emplace_back(v.unwrap());
            });

        return res;
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
        std::lock_guard lock{mutex_};
        byLedger(
            lock,
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeID const&, Validation const& v) {
                if (v.trusted() && v.full())
                {
                    std::optional<std::uint32_t> loadFee = v.loadFee();
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
        std::lock_guard lock{mutex_};
        current_.clear();
    }

    /** Return quantity of lagging proposers, and remove online proposers
     *  for purposes of evaluating whether to pause.
     *
     *  Laggards are the trusted proposers whose sequence number is lower
     *  than the sequence number from which our current pending proposal
     *  is based. Proposers from whom we have not received a validation for
     *  awhile are considered offline.
     *
     *  Note: the trusted flag is not used in this evaluation because it's made
     *  redundant by checking the list of proposers.
     *
     * @param seq Our current sequence number.
     * @param trustedKeys Public keys of trusted proposers.
     * @return Quantity of laggards.
     */
    std::size_t
    laggards(Seq const seq, hash_set<NodeKey>& trustedKeys)
    {
        std::size_t laggards = 0;

        current(
            std::lock_guard{mutex_},
            [](std::size_t) {},
            [&](NodeID const&, Validation const& v) {
                if (adaptor_.now() <
                        v.seenTime() + parms_.validationFRESHNESS &&
                    trustedKeys.find(v.key()) != trustedKeys.end())
                {
                    trustedKeys.erase(v.key());
                    if (seq > v.seq())
                        ++laggards;
                }
            });

        return laggards;
    }
};

}  // namespace ripple
#endif
