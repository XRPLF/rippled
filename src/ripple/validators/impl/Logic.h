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

#ifndef RIPPLE_VALIDATORS_LOGIC_H_INCLUDED
#define RIPPLE_VALIDATORS_LOGIC_H_INCLUDED

#include <ripple/validators/impl/Store.h>
#include <ripple/validators/impl/ChosenList.h>
#include <ripple/validators/impl/Validation.h>
#include <ripple/validators/impl/Validator.h>
#include <ripple/validators/impl/Tuning.h>
#include <beast/chrono/manual_clock.h>
#include <beast/container/aged_unordered_set.h>
#include <beast/smart_ptr/SharedPtr.h>
#include <memory>

namespace ripple {
namespace Validators {

// Forward declare unit test so it can be a friend to LRUCache.
class Logic_test;

namespace detail {

// The LRUCache class (ab)uses an aged_unordered_set so it can hold on
// to a limited number of values.  When the container gets too full the
// LRUCache expires the oldest values.
//
// An aged_unordered_set gives us the functionality we want by keeping the
// chronological list.  We don't care about the actual time of entry, only
// the time ordering.  So we hook the aged_unordered_set up to a maunual_clock
// (which we never bother to increment).
//
// The implementation could potentially be changed to be time-based, rather
// than count-based, by hooking up a beast::basic_second_clock in place of the
// manual_clock and deleting a range of expired entries on insert.
//
template <class Key,
          class Hash = std::hash <Key>,
          class KeyEqual = std::equal_to <Key>,
          class Allocator = std::allocator <Key> >
class LRUCache
{
private:
    typedef beast::manual_clock <std::chrono::steady_clock> Clock;
    typedef beast::aged_unordered_set <
        Key, std::chrono::steady_clock, Hash, KeyEqual, Allocator> ContainerType;

public:
    LRUCache () = delete;

    LRUCache (LRUCache const& lhs) = delete;

    explicit LRUCache (
        size_t item_max,
        Hash hash = Hash(),
        KeyEqual equal = KeyEqual(),
        Allocator alloc = Allocator())
    : m_clock ()
    , m_cache (m_clock, hash, equal, alloc)
    , m_item_max (item_max)
    {
        m_cache.reserve (m_item_max + 1);
    }

    LRUCache& operator= (LRUCache const& lhs) = delete;

    // Add the entry.  Remove the oldest entry if we went over our limit.
    // Returns true on insertion (the entry was not already in the cache).
    bool insert (Key const& key)
    {
        auto const insertRet (m_cache.insert (key));
        if (insertRet.second == false)
        {
            // key is re-referenced.  Mark it as MRU.
            m_cache.touch (insertRet.first);
        }
        else if (m_cache.size () > m_item_max)
        {
            // Added key and cache is too big.  Erase oldest element.
            m_cache.erase (m_cache.chronological.begin ());
        }
        return insertRet.second;
    }

    size_t size ()
    {
        return m_cache.size();
    }

    Key const* oldest ()
    {
        return m_cache.empty() ? nullptr : &(*m_cache.chronological.begin());
    }

private:
    Clock m_clock;
    ContainerType m_cache;
    const size_t m_item_max;
};
} // namespace detail

//------------------------------------------------------------------------------

// Encapsulates the logic for creating the chosen validators.
// This is a separate class to facilitate the unit tests.
//
class Logic
{
public:
    struct State
    {
        State ()
            : stopping (false)
            { }

        /** True if we are stopping. */
        bool stopping;

        /** The source we are currently fetching. */
        beast::SharedPtr <Source> fetchSource;
    };

private:
    typedef beast::SharedData <State> SharedState;

    SharedState m_state;

    Store& m_store;
    beast::Journal m_journal;

    // A small integer assigned to each closed ledger
    //
    int m_ledgerID;

    // The chosen set of trusted validators (formerly the "UNL")
    //
    bool m_rebuildChosenList;
    ChosenList::Ptr m_chosenList;

    // Holds the list of sources
    //
    typedef std::vector <SourceDesc> SourceTable;
    SourceTable m_sources;

    // Holds the internal list of trusted validators
    //
    typedef hardened_hash_map <RipplePublicKey, Validator> ValidatorTable;
    ValidatorTable m_validators;

    // Filters duplicate validations
    //
    typedef detail::LRUCache <ReceivedValidation,
                              ReceivedValidationHash,
                              ReceivedValidationKeyEqual> RecentValidations;
    RecentValidations m_recentValidations;

    // Filters duplicate ledger hashes
    //
    typedef detail::LRUCache <RippleLedgerHash,
                              RippleLedgerHash::hasher,
                              RippleLedgerHash::key_equal> RecentLedgerHashes;
    RecentLedgerHashes m_recentLedgerHashes;

    //--------------------------------------------------------------------------
public:

    explicit Logic (Store& store, beast::Journal journal = beast::Journal ())
        : m_store (store)
        , m_journal (journal)
        , m_ledgerID (0)
        , m_rebuildChosenList (false)
        , m_recentValidations (recentValidationsCacheSize)
        , m_recentLedgerHashes (recentLedgersCacheSize)
    {
        m_sources.reserve (16);
    }

    /** Stop the logic.
        This will cancel the current fetch and set the stopping flag
        to `true` to prevent further fetches.
        Thread safety:
            Safe to call from any thread.
    */
    void stop ()
    {
        SharedState::Access state (m_state);
        state->stopping = true;
        if (state->fetchSource != nullptr)
            state->fetchSource->cancel ();
    }

    //--------------------------------------------------------------------------

    void load ()
    {
        // load data from the database
    }

    // Returns `true` if a Source with the same unique ID already exists
    //
    bool findSourceByID (std::string id)
    {
        for (SourceTable::const_iterator iter (m_sources.begin());
            iter != m_sources.end(); ++iter)
            if (iter->source->uniqueID() == id)
                return true;
        return false;
    }

    // Add a one-time static source.
    // Fetch is called right away, this call blocks.
    //
    void addStatic (beast::SharedPtr <Source> source)
    {
        if (findSourceByID (source->uniqueID()))
        {
            m_journal.error <<
                "Duplicate static " << *source;
            return;
        }

        m_journal.trace <<
            "Addding static " << *source;

        Source::Results results;
        source->fetch (results, m_journal);

        if (results.success)
        {
            std::size_t const numAdded (merge (results.list, source));
            m_journal.trace <<
                "Added " << numAdded <<
                " trusted validators from " << *source;
        }
        else
        {
            // TODO: Report the error
        }
    }

    // Add a live source to the list of sources.
    //
    void add (beast::SharedPtr <Source> source)
    {
        if (findSourceByID (source->uniqueID()))
        {
            std::unique_ptr <Source> object (source);
            m_journal.error <<
                "Duplicate " << *source;
            return;
        }

        m_journal.info <<
            "Adding " << *source;

        {
            m_sources.resize (m_sources.size() + 1);
            SourceDesc& desc (m_sources.back());
            desc.source = source;
            m_store.insert (desc);
            merge (desc.results.list, desc.source);
        }
    }

    // Add each entry in the list to the map, incrementing the
    // reference count if it already exists, and updating fields.
    //
    std::size_t merge (std::vector <Source::Item> const& list, Source* source)
    {
        std::size_t numAdded (0);
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Item const& item (list [i]);
            std::pair <ValidatorTable::iterator, bool> results (
                m_validators.emplace (item.publicKey, Validator ()));
            Validator& validatorInfo (results.first->second);
            validatorInfo.addRef();
            if (results.second)
            {
                // This is a new one
                ++numAdded;
                dirtyChosen ();
            }
        }

        return numAdded;
    }

    // Decrement the reference count of each item in the list
    // in the map.
    //
    std::size_t remove (std::vector <Source::Item> const& list,
        Source* source)
    {
        std::size_t numRemoved (0);
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Item const& item (list [i]);
            ValidatorTable::iterator iter (m_validators.find (item.publicKey));
            bassert (iter != m_validators.end ());
            Validator& validatorInfo (iter->second);
            if (validatorInfo.release())
            {
                // Last reference removed
                ++numRemoved;
                m_validators.erase (iter);
                dirtyChosen ();
            }
        }

        return numRemoved;
    }

    /** Return reference to m_sources for Mangager::PropertyStream. */
    SourceTable const& getSources ()
    {
        return m_sources;
    }

    /** Return reference to m_validators for Manager::PropertyStream. */
    ValidatorTable const& getValidators ()
    {
        return m_validators;
    }

    //--------------------------------------------------------------------------
    //
    // Chosen
    //
    //--------------------------------------------------------------------------

    /** Rebuild the Chosen List. */
    void buildChosen ()
    {
        ChosenList::Ptr list (new ChosenList (m_validators.size ()));

        for (ValidatorTable::const_iterator iter = m_validators.begin ();
            iter != m_validators.end (); ++iter)
        {
            ChosenList::Info item;
            list->insert (iter->first, item);
        }

        // This is thread safe
        m_chosenList = list;

        m_journal.debug <<
            "Rebuilt chosen list with " <<
            std::to_string (m_chosenList->size()) << " entries";
    }

    /** Mark the Chosen List for a rebuild. */
    void dirtyChosen ()
    {
        m_rebuildChosenList = true;
    }

    /** Rebuild the Chosen List if necessary. */
    void checkChosen ()
    {
        if (m_rebuildChosenList)
        {
            buildChosen ();
            m_rebuildChosenList = false;
        }
    }

    /** Returns number of elements in the current Chosen list. */
    std::uint32_t getChosenSize()
    {
        return m_chosenList ? m_chosenList->size() : 0;
    }

    //--------------------------------------------------------------------------
    //
    // Fetching
    //
    //--------------------------------------------------------------------------

    /** Perform a fetch on the source. */
    void fetch (SourceDesc& desc)
    {
        beast::SharedPtr <Source> const& source (desc.source);
        Source::Results results;

        {
            {
                SharedState::Access state (m_state);
                if (state->stopping)
                    return;
                state->fetchSource = source;
            }

            source->fetch (results, m_journal);

            {
                SharedState::Access state (m_state);
                if (state->stopping)
                    return;
                state->fetchSource = nullptr;
            }
        }

        // Reset fetch timer for the source->
        desc.whenToFetch = beast::Time::getCurrentTime () +
            beast::RelativeTime (secondsBetweenFetches);

        if (results.success)
        {
            // Count the number fetched
            std::size_t const numFetched (
                results.list.size());

            // Add the new source item to the map
            std::size_t const numAdded (
                merge (results.list, source));

            // Swap lists
            std::swap (desc.results, results);

            // Remove the old source item from the map
            std::size_t const numRemoved (remove (results.list, source));

            // Report
            if (numAdded > numRemoved)
            {
                m_journal.info <<
                    "Fetched " << numFetched <<
                    "(" << (numAdded - numRemoved) << " new) " <<
                    " trusted validators from " << *source;
            }
            else if (numRemoved > numAdded)
            {
                m_journal.info <<
                    "Fetched " << numFetched <<
                    "(" << numRemoved - numAdded << " removed) " <<
                    " trusted validators from " << *source;
            }
            else
            {
                m_journal.debug <<
                    "Fetched " << numFetched <<
                    " trusted validators from " << *source;
            }

            // See if we need to rebuild
            checkChosen ();

            // Reset failure status
            desc.numberOfFailures = 0;
            desc.status = SourceDesc::statusFetched;

            // Update the source's list in the store
            m_store.update (desc, true);
        }
        else
        {
            m_journal.error <<
                "Failed to fetch " << *source;

            ++desc.numberOfFailures;
            desc.status = SourceDesc::statusFailed;
            // Record the failure in the Store
            m_store.update (desc);
        }
    }

    /** Expire a source's list of validators. */
    void expire (SourceDesc& desc)
    {
        // Decrement reference count on each validator
        remove (desc.results.list, desc.source);

        m_store.update (desc);
    }

    /** Process up to one source that needs fetching.
        @return The number of sources that were fetched.
    */
    std::size_t fetch_one ()
    {
        std::size_t n (0);
        beast::Time const currentTime (beast::Time::getCurrentTime ());

        for (SourceTable::iterator iter = m_sources.begin ();
            (n == 0) && iter != m_sources.end (); ++iter)
        {
            SourceDesc& desc (*iter);

            // See if we should fetch
            //
            if (desc.whenToFetch <= currentTime)
            {
                fetch (desc);
                ++n;
            }

            // See if we need to expire
            //
            if (desc.expirationTime.isNotNull () &&
                desc.expirationTime <= currentTime)
            {
                expire (desc);
            }
        }

        return n;
    }

    //--------------------------------------------------------------------------
    //
    // Ripple interface
    //
    //--------------------------------------------------------------------------

    // Called when we receive a signed validation
    //
    void receiveValidation (ReceivedValidation const& rv)
    {
        // Accept validation from the trusted list
        ValidatorTable::iterator iter (m_validators.find (rv.publicKey));
        if (iter != m_validators.end ())
        {
            // Filter duplicates (defensive programming)
            if (! m_recentValidations.insert (rv))
                return;

            iter->second.on_validation (rv.ledgerHash);

            m_journal.trace <<
                "New trusted validation for " << rv.ledgerHash <<
                " from " << rv.publicKey;
        }
        else
        {
            m_journal.trace <<
                "Untrusted validation for " << rv.ledgerHash <<
                " from " << rv.publicKey;
        }
    }

    // Called when a ledger is closed
    //
    void ledgerClosed (RippleLedgerHash const& ledgerHash)
    {
        // Filter duplicates (defensive programming)
        if (! m_recentLedgerHashes.insert (ledgerHash))
            return;

        ++m_ledgerID;

        m_journal.trace <<
            "Closed ledger " << m_ledgerID;

        for (ValidatorTable::iterator iter (m_validators.begin());
            iter != m_validators.end(); ++iter)
            iter->second.on_ledger (ledgerHash);
    }

    // Returns `true` if the public key hash is contained in the Chosen List.
    //
    bool isTrustedPublicKeyHash (RipplePublicKeyHash const& publicKeyHash)
    {
        return m_chosenList->containsPublicKeyHash (publicKeyHash);
    }

    //--------------------------------------------------------------------------
};

}
}

#endif
