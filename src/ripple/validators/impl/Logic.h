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

#include <memory>
#include <unordered_map>

namespace ripple {
namespace Validators {

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
    typedef ripple::unordered_map <
        RipplePublicKey, Validator,
            beast::hardened_hash<RipplePublicKey>> ValidatorTable;
    ValidatorTable m_validators;

    // Filters duplicate validations
    //
    typedef CycledSet <ReceivedValidation,
                       ReceivedValidationHash,
                       ReceivedValidationKeyEqual> SeenValidations;
    SeenValidations m_seenValidations;

    // Filters duplicate ledger hashes
    //
    typedef CycledSet <RippleLedgerHash,
                       RippleLedgerHash::hasher,
                       RippleLedgerHash::key_equal> SeenLedgerHashes;
    SeenLedgerHashes m_seenLedgerHashes;

    //--------------------------------------------------------------------------

    explicit Logic (Store& store, beast::Journal journal = beast::Journal ())
        : m_store (store)
        , m_journal (journal)
        , m_ledgerID (0)
        , m_rebuildChosenList (false)
        , m_seenValidations (seenValidationsCacheSize)
        , m_seenLedgerHashes (seenLedgersCacheSize)
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
    bool findSourceByID (beast::String id)
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
            beast::String::fromNumber (m_chosenList->size()) << " entries";
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

    /** Returns the current Chosen list.
        This can be called from any thread at any time.
    */
    ChosenList::Ptr getChosen ()
    {
        return m_chosenList;
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
            if (! m_seenValidations.insert (rv))
                return;

            iter->second.receiveValidation (rv.ledgerHash);

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
        if (! m_seenLedgerHashes.insert (ledgerHash))
            return;

        ++m_ledgerID;

        m_journal.trace <<
            "Closed ledger " << m_ledgerID;

        for (ValidatorTable::iterator iter (m_validators.begin());
            iter != m_validators.end(); ++iter)
        {
            Validator& v (iter->second);
            v.ledgerClosed (ledgerHash);
        }
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
