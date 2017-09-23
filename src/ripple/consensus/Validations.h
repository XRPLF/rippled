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
#include <ripple/beast/utility/Journal.h>
#include <ripple/beast/utility/Zero.h>
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

/** Maintains current and recent ledger validations.

    Manages storage and queries related to validations received on the network.
    Stores the most current validation from nodes and sets of recent
    validations grouped by ledger identifier.

    Stored validations are not necessarily from trusted nodes, so clients
    and implementations should take care to use `trusted` member functions or
    check the validation's trusted status.

    This class uses a policy design to allow adapting the handling of stale
    validations in various circumstances. Below is a set of stubs illustrating
    the required type interface.

    @warning The MutexType is used to manage concurrent access to private
             members of Validations but does not manage any data in the
             StalePolicy instance.

    @code

    // Identifier types that should be equality-comparable and copyable
    struct LedgerID;
    struct NodeID;
    struct NodeKey;

    struct Validation
    {
        // Ledger ID associated with this validation
        LedgerID ledgerID() const;

        // Sequence number of validation's ledger (0 means no sequence number)
        std::uint32_t seq() const

        // When the validation was signed
        NetClock::time_point signTime() const;

        // When the validation was first observed by this node
        NetClock::time_point seenTime() const;

        // Signing key of node that published the validation
        NodeKey key() const;

        // Identifier of node that published the validation
        NodeID nodeID() const;

        // Whether the publishing node was trusted at the time the validation
        // arrived
        bool trusted() const;

        implementation_specific_t
        unwrap() -> return the implementation-specific type being wrapped

        // ... implementation specific
    };

    class StalePolicy
    {
        // Handle a newly stale validation, this should do minimal work since
        // it is called by Validations while it may be iterating Validations
        // under lock
        void onStale(Validation && );

        // Flush the remaining validations (typically done on shutdown)
        void flush(hash_map<NodeKey,Validation> && remaining);

        // Return the current network time (used to determine staleness)
        NetClock::time_point now() const;

        // ... implementation specific
    };
    @endcode

    @tparam StalePolicy Determines how to determine and handle stale validations
    @tparam Validation Conforming type representing a ledger validation
    @tparam MutexType Mutex used to manage concurrent access

*/
template <class StalePolicy, class Validation, class MutexType>
class Validations
{
    template <typename T>
    using decay_result_t = std::decay_t<std::result_of_t<T>>;

    using WrappedValidationType =
        decay_result_t<decltype (&Validation::unwrap)(Validation)>;
    using LedgerID =
        decay_result_t<decltype (&Validation::ledgerID)(Validation)>;
    using NodeKey = decay_result_t<decltype (&Validation::key)(Validation)>;
    using NodeID = decay_result_t<decltype (&Validation::nodeID)(Validation)>;


    using ScopedLock = std::lock_guard<MutexType>;

    // Manages concurrent access to current_ and byLedger_
    MutexType mutex_;

    //! For the most recent validation, we also want to store the ID
    //! of the ledger it replaces
    struct ValidationAndPrevID
    {
        ValidationAndPrevID(Validation const& v) : val{v}, prevLedgerID{0}
        {
        }

        Validation val;
        LedgerID prevLedgerID;
    };

    //! The latest validation from each node
    hash_map<NodeKey, ValidationAndPrevID> current_;

    //! Recent validations from nodes, indexed by ledger identifier
    beast::aged_unordered_map<
        LedgerID,
        hash_map<NodeKey, Validation>,
        std::chrono::steady_clock,
        beast::uhash<>>
        byLedger_;

    //! Parameters to determine validation staleness
    ValidationParms const parms_;

    beast::Journal j_;

    //! StalePolicy details providing now(), onStale() and flush() callbacks
    //! Is NOT managed by the mutex_ above
    StalePolicy stalePolicy_;

private:
    /** Iterate current validations.

        Iterate current validations, optionally removing any stale validations
        if a time is specified.

        @param t (Optional) Time used to determine staleness
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
    current(boost::optional<NetClock::time_point> t, Pre&& pre, F&& f)
    {
        ScopedLock lock{mutex_};
        pre(current_.size());
        auto it = current_.begin();
        while (it != current_.end())
        {
            // Check for staleness, if time specified
            if (t &&
                !isCurrent(
                    parms_, *t, it->second.val.signTime(), it->second.val.seenTime()))
            {
                // contains a stale record
                stalePolicy_.onStale(std::move(it->second.val));
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
    byLedger(LedgerID const& ledgerID, Pre&& pre, F&& f)
    {
        ScopedLock lock{mutex_};
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
        @param j Journal used for logging
        @param ts Parameters for constructing StalePolicy instance
    */
    template <class... Ts>
    Validations(
        ValidationParms const& p,
        beast::abstract_clock<std::chrono::steady_clock>& c,
        beast::Journal j,
        Ts&&... ts)
        : byLedger_(c), parms_(p), j_(j), stalePolicy_(std::forward<Ts>(ts)...)
    {
    }

    /** Return the validation timing parameters
     */
    ValidationParms const&
    parms() const
    {
        return parms_;
    }

    /** Return the journal
     */
    beast::Journal
    journal() const
    {
        return j_;
    }

    /** Result of adding a new validation
     */
    enum class AddOutcome {
        /// This was a new validation and was added
        current,
        /// Already had this validation
        repeat,
        /// Not current or was older than current from this node
        stale,
        /// Had a validation with same sequence number
        sameSeq,
    };

    /** Add a new validation

        Attempt to add a new validation.

        @param key The NodeKey to use for the validation
        @param val The validation to store
        @return The outcome of the attempt

        @note The provided key may differ from the validation's
              key() member since we might be storing by master key and the
              validation might be signed by a temporary or rotating key.

    */
    AddOutcome
    add(NodeKey const& key, Validation const& val)
    {
        NetClock::time_point t = stalePolicy_.now();
        if (!isCurrent(parms_, t, val.signTime(), val.seenTime()))
            return AddOutcome::stale;

        LedgerID const& id = val.ledgerID();

        // This is only seated if a validation became stale
        boost::optional<Validation> maybeStaleValidation;

        AddOutcome result = AddOutcome::current;

        {
            ScopedLock lock{mutex_};

            auto const ret = byLedger_[id].emplace(key, val);

            // This validation is a repeat if we already have
            // one with the same id and signing key.
            if (!ret.second && ret.first->second.key() == val.key())
                return AddOutcome::repeat;

            // Attempt to insert
            auto const ins = current_.emplace(key, val);

            if (!ins.second)
            {
                // Had a previous validation from the node, consider updating
                Validation& oldVal = ins.first->second.val;
                LedgerID const previousLedgerID = ins.first->second.prevLedgerID;

                std::uint32_t const oldSeq{oldVal.seq()};
                std::uint32_t const newSeq{val.seq()};

                // Sequence of 0 indicates a missing sequence number
                if (oldSeq && newSeq && oldSeq == newSeq)
                {
                    result = AddOutcome::sameSeq;

                    // If the validation key was revoked, update the
                    // existing validation in the byLedger_ set
                    if (val.key() != oldVal.key())
                    {
                        auto const mapIt = byLedger_.find(oldVal.ledgerID());
                        if (mapIt != byLedger_.end())
                        {
                            auto& validationMap = mapIt->second;
                            // If a new validation with the same ID was
                            // reissued we simply replace.
                            if(oldVal.ledgerID() == val.ledgerID())
                            {
                                auto replaceRes = validationMap.emplace(key, val);
                                // If it was already there, replace
                                if(!replaceRes.second)
                                    replaceRes.first->second = val;
                            }
                            else
                            {
                                // If the new validation has a different ID,
                                // we remove the old.
                                validationMap.erase(key);
                                // Erase the set if it is now empty
                                if (validationMap.empty())
                                    byLedger_.erase(mapIt);
                            }
                        }
                    }
                }

                if (val.signTime() > oldVal.signTime() ||
                    val.key() != oldVal.key())
                {
                    // This is either a newer validation or a new signing key
                    LedgerID const prevID = [&]() {
                        // In the normal case, the prevID is the ID of the
                        // ledger we replace
                        if (oldVal.ledgerID() != val.ledgerID())
                            return oldVal.ledgerID();
                        // In the case the key was revoked and a new validation
                        // for the same ledger ID was sent, the previous ledger
                        // is still the one the now revoked validation had
                        return previousLedgerID;
                    }();

                    // Allow impl to take over oldVal
                    maybeStaleValidation.emplace(std::move(oldVal));
                    // Replace old val in the map and set the previous ledger ID
                    ins.first->second.val = val;
                    ins.first->second.prevLedgerID = prevID;
                }
                else
                {
                    // We already have a newer validation from this source
                    result = AddOutcome::stale;
                }
            }
        }

        // Handle the newly stale validation outside the lock
        if (maybeStaleValidation)
        {
            stalePolicy_.onStale(std::move(*maybeStaleValidation));
        }

        return result;
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

    /** Distribution of current trusted validations

        Calculates the distribution of current validations but allows
        ledgers one away from the current ledger to count as the current.

        @param currentLedger The identifier of the ledger we believe is current
        @param priorLedger The identifier of our previous current ledger
        @param cutoffBefore Ignore ledgers with sequence number before this

        @return Map representing the distribution of ledgerID by count
    */
    hash_map<LedgerID, std::uint32_t>
    currentTrustedDistribution(
        LedgerID const& currentLedger,
        LedgerID const& priorLedger,
        std::uint32_t cutoffBefore)
    {
        bool const valCurrentLedger = currentLedger != beast::zero;
        bool const valPriorLedger = priorLedger != beast::zero;

        hash_map<LedgerID, std::uint32_t> ret;

        current(
            stalePolicy_.now(),
            // The number of validations does not correspond to the number of
            // distinct ledgerIDs so we do not call reserve on ret.
            [](std::size_t) {},
            [this,
             &cutoffBefore,
             &currentLedger,
             &valCurrentLedger,
             &valPriorLedger,
             &priorLedger,
             &ret](NodeKey const&, ValidationAndPrevID const& vp) {
                Validation const& v = vp.val;
                LedgerID const& prevLedgerID = vp.prevLedgerID;
                if (!v.trusted())
                    return;

                std::uint32_t const seq = v.seq();
                if ((seq == 0) || (seq >= cutoffBefore))
                {
                    // contains a live record
                    bool countPreferred =
                        valCurrentLedger && (v.ledgerID() == currentLedger);

                    if (!countPreferred &&  // allow up to one ledger slip in
                                            // either direction
                        ((valCurrentLedger &&
                          (prevLedgerID == currentLedger)) ||
                         (valPriorLedger && (v.ledgerID() == priorLedger))))
                    {
                        countPreferred = true;
                        JLOG(this->j_.trace()) << "Counting for " << currentLedger
                                         << " not " << v.ledgerID();
                    }

                    if (countPreferred)
                        ret[currentLedger]++;
                    else
                        ret[v.ledgerID()]++;
                }
            });

        return ret;
    }

    /** Count the number of current trusted validators working on the next
        ledger.

        Counts the number of current trusted validations that replaced the
        provided ledger.  Does not check or update staleness of the validations.

        @param ledgerID The identifier of the preceding ledger of interest
        @return The number of current trusted validators with ledgerID as the
                prior ledger.
    */
    std::size_t
    getNodesAfter(LedgerID const& ledgerID)
    {
        std::size_t count = 0;

        // Historically this did not not check for stale validations
        // That may not be important, but this preserves the behavior
        current(
            boost::none,
            [&](std::size_t) {}, // nothing to reserve
            [&](NodeKey const&, ValidationAndPrevID const& v) {
                if (v.val.trusted() && v.prevLedgerID == ledgerID)
                    ++count;
            });
        return count;
    }

    /** Get the currently trusted validations

        @return Vector of validations from currently trusted validators
    */
    std::vector<WrappedValidationType>
    currentTrusted()
    {
        std::vector<WrappedValidationType> ret;

        current(
            stalePolicy_.now(),
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeKey const&, ValidationAndPrevID const& v) {
                if (v.val.trusted())
                    ret.push_back(v.val.unwrap());
            });
        return ret;
    }

    /** Get the set of known public keys associated with current validations

        @return The set of of knowns keys for current trusted and untrusted
                validations
    */
    hash_set<NodeKey>
    getCurrentPublicKeys()
    {
        hash_set<NodeKey> ret;
        current(
            stalePolicy_.now(),
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeKey const& k, ValidationAndPrevID const&) { ret.insert(k); });

        return ret;
    }

    /** Count the number of trusted validations for the given ledger

        @param ledgerID The identifier of ledger of interest
        @return The number of trusted validations
    */
    std::size_t
    numTrustedForLedger(LedgerID const& ledgerID)
    {
        std::size_t count = 0;
        byLedger(
            ledgerID,
            [&](std::size_t) {}, // nothing to reserve
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted())
                    ++count;
            });
        return count;
    }

    /**  Get set of trusted validations associated with a given ledger

         @param ledgerID The identifier of ledger of interest
         @return Trusted validations associated with ledger
    */
    std::vector<WrappedValidationType>
    getTrustedForLedger(LedgerID const& ledgerID)
    {
        std::vector<WrappedValidationType> res;
        byLedger(
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted())
                    res.emplace_back(v.unwrap());
            });

        return res;
    }

    /** Return the sign times of all validations associated with a given ledger

        @param ledgerID The identifier of ledger of interest
        @return Vector of times
    */
    std::vector<NetClock::time_point>
    getTrustedValidationTimes(LedgerID const& ledgerID)
    {
        std::vector<NetClock::time_point> times;
        byLedger(
            ledgerID,
            [&](std::size_t numValidations) { times.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted())
                    times.emplace_back(v.signTime());
            });
        return times;
    }

    /** Returns fees reported by trusted validators in the given ledger

        @param ledgerID The identifier of ledger of interest
        @param baseFee The fee to report if not present in the validation
        @return Vector of fees
    */
    std::vector<std::uint32_t>
    fees(LedgerID const& ledgerID, std::uint32_t baseFee)
    {
        std::vector<std::uint32_t> res;
        byLedger(
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted())
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
        JLOG(j_.info()) << "Flushing validations";

        hash_map<NodeKey, Validation> flushed;
        {
            ScopedLock lock{mutex_};
            for (auto it : current_)
            {
                flushed.emplace(it.first, std::move(it.second.val));
            }
            current_.clear();
        }

        stalePolicy_.flush(std::move(flushed));

        JLOG(j_.debug()) << "Validations flushed";
    }
};
}  // namespace ripple
#endif
