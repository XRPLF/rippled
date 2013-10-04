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
        {
            //sources.reserve (64);
        }

        ValidatorMap validators;
        SourcesType sources;
    };

    typedef SharedData <State> SharedState;

    Store& m_store;
    Journal m_journal;
    bool m_rebuildChosenList;
    ChosenList::Ptr m_chosenList;
    SharedState m_state;

    // Used to filter duplicate ledger hashes
    //
    typedef AgedHistory <boost::unordered_set <
        RippleLedgerHash, RippleLedgerHash::hasher> > SeenLedgerHashes;
    SeenLedgerHashes m_seenLedgerHashes;

    //----------------------------------------------------------------------

    explicit Logic (Store& store, Journal journal = Journal ())
        : m_store (store)
        , m_journal (journal)
        , m_rebuildChosenList (false)
    {
    }

    void load ()
    {
        // load data from m_store
    }

    // Returns `true` if a Source with the same unique ID already exists
    //
    bool findSourceByID (String id)
    {
        SharedState::Access state (m_state);
        for (SourcesType::const_iterator iter (state->sources.begin());
            iter != state->sources.end(); ++iter)
            if (iter->source->uniqueID() == id)
                return true;
        return false;
    }

    // Add a one-time static source.
    // Fetch is called right away, this call blocks.
    //
    void addStatic (SharedPtr <Source> source)
    {
        if (findSourceByID (source->uniqueID()))
        {
            m_journal.error << "Duplicate static " << source->name();
            return;
        }

        m_journal.info << "Addding static " << source->name();

        Source::Result result;
        source->fetch (result, m_journal);

        if (result.success)
        {
            SharedState::Access state (m_state);
            std::size_t const numAdded (
                merge (result.list, source, state));
            m_journal.info << "Added " << numAdded
                           << " trusted validators from " << source->name();
        }
        else
        {
            // TODO: Report the error
        }
    }

    // Add a live source to the list of sources.
    //
    void add (SharedPtr <Source> source)
    {
        if (findSourceByID (source->uniqueID()))
        {
            ScopedPointer <Source> object (source);
            m_journal.error << "Duplicate " << source->name();
            return;
        }

        m_journal.info << "Adding " << source->name();

        {
            SharedState::Access state (m_state);
            state->sources.resize (state->sources.size() + 1);
            SourceDesc& desc (state->sources.back());
            desc.source = source;
            m_store.insert (desc);
            merge (desc.result.list, desc.source, state);
        }
    }

    // Add each entry in the list to the map, incrementing the
    // reference count if it already exists, and updating fields.
    //
    std::size_t merge (std::vector <Source::Info> const& list,
        Source* source, SharedState::Access& state)
    {
        std::size_t numAdded (0);
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list [i]);
            std::pair <ValidatorMap::iterator, bool> result (
                state->validators.emplace (info.publicKey, Validator ()));
            Validator& validatorInfo (result.first->second);
            ++validatorInfo.refCount;
            if (result.second)
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
    std::size_t remove (std::vector <Source::Info> const& list,
        Source* source, SharedState::Access& state)
    {
        std::size_t numRemoved (0);
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list [i]);
            ValidatorMap::iterator iter (state->validators.find (info.publicKey));
            bassert (iter != state->validators.end ());
            Validator& validatorInfo (iter->second);
            if (--validatorInfo.refCount == 0)
            {
                // Last reference removed
                ++numRemoved;
                state->validators.erase (iter);
                dirtyChosen ();
            }
        }

        return numRemoved;
    }

    //----------------------------------------------------------------------
    //
    // Chosen
    //

    /** Rebuild the Chosen List. */
    void buildChosen ()
    {
        SharedState::ConstAccess state (m_state);
        ChosenList::Ptr list (new ChosenList (state->validators.size ()));

        for (ValidatorMap::const_iterator iter = state->validators.begin ();
            iter != state->validators.end (); ++iter)
        {
            ChosenList::Info info;
            list->insert (iter->first, info);
        }

        // This is thread safe
        m_chosenList = list;

        m_journal.debug <<
            "Rebuilt chosen list with " <<
            String::fromNumber (m_chosenList->size()) << " entries";
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

    //----------------------------------------------------------------------
    //
    // Fetching
    //

    /** Perform a fetch on the source. */
    void fetch (SourceDesc& desc)
    {
        Source* const source (desc.source);

        Source::Result result;
        source->fetch (result, m_journal);

        // Reset fetch timer for the source.
        desc.whenToFetch = Time::getCurrentTime () +
            RelativeTime (secondsBetweenFetches);

        if (result.success)
        {
            SharedState::Access state (m_state);

            // Count the number fetched
            std::size_t const numFetched (
                result.list.size());

            // Add the new source info to the map
            std::size_t const numAdded (
                merge (result.list, source, state));

            // Swap lists
            desc.result.swapWith (result);

            // Remove the old source info from the map
            std::size_t const numRemoved (
                remove (result.list, source, state));

            // Report
            if (numAdded > numRemoved)
            {
                m_journal.info <<
                    "Fetched " << numFetched <<
                    "(" << (numAdded - numRemoved) << " new) " <<
                    " trusted validators from " << source->name();
            }
            else if (numRemoved > numAdded)
            {
                m_journal.info <<
                    "Fetched " << numFetched <<
                    "(" << numRemoved - numAdded << " removed) " <<
                    " trusted validators from " << source->name();
            }
            else
            {
                m_journal.info <<
                    "Fetched " << numFetched <<
                    " trusted validators from " << source->name();
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
            m_journal.error << "Failed to fetch " << source->name();

            ++desc.numberOfFailures;
            desc.status = SourceDesc::statusFailed;

            // Record the failure in the Store
            m_store.update (desc);
        }
    }

    /** Expire a source's list of validators. */
    void expire (SourceDesc& desc, SharedState::Access& state)
    {
        // Decrement reference count on each validator
        remove (desc.result.list, desc.source, state);

        m_store.update (desc);
    }

    /** Process up to one source that needs fetching.
        @return The number of sources that were fetched.
    */
    std::size_t fetch_one ()
    {
        std::size_t n (0);
        Time const currentTime (Time::getCurrentTime ());
        
        SharedState::Access state (m_state);
        for (SourcesType::iterator iter = state->sources.begin ();
            (n == 0) && iter != state->sources.end (); ++iter)
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
                expire (desc, state);
            }
        }

        return n;
    }

    //----------------------------------------------------------------------
    // 
    // RPC Handlers
    //

    // Return the current ChosenList as JSON
    Json::Value rpcPrint (Json::Value const& args)
    {
        Json::Value result (Json::objectValue);

#if 0
        Json::Value entries (Json::arrayValue);
        ChosenList::Ptr list (m_chosenList);
        if (list != nullptr)
        {
            for (ChosenList::ValidatorMap::const_iterator iter (list->map().begin());
                iter != list->map().end(); ++iter)
            {
                Json::Value entry (Json::objectValue);
                ChosenList::ValidatorMap::key_type const& key (iter->first);
                entry ["key"] = key.to_string();
                //ChosenList::ValidatorMap::mapped_type const& value (iter->second);
                //entry ["value"] = value.to_string();
                entries.append (entry);
            }
        }
        result ["chosen_list"] = entries;

       {
            SharedState::ConstAccess state (m_state);
            std::size_t count (0);
            result ["validators"] = state->validators.size();
            for (ValidatorMap::const_iterator iter (state->validators.begin());
                iter != state->validators.end(); ++iter)
                count += iter->second.map.size();
            result ["signatures"] = count;
        }
#else
        Json::Value entries (Json::arrayValue);
        {
            SharedState::ConstAccess state (m_state);
            result ["count"] = int(state->validators.size());
            for (ValidatorMap::const_iterator iter (state->validators.begin());
                iter != state->validators.end(); ++iter)
            {
                Validator const& v (iter->second);
                Json::Value entry (Json::objectValue);

                std::size_t const closed (
                    v.count->closed + v.count.back().closed);

                std::size_t const seen (
                    v.count->seen + v.count.back().seen);

                std::size_t const missing (
                    v.count->missing + v.count.back().missing);

                std::size_t const orphans (
                    v.count->orphans + v.count.back().orphans);

                entry ["public"]  = iter->first.to_string();
                entry ["closed"]  = int(closed);
                entry ["seen"]    = int(seen);
                entry ["missing"] = int(missing);
                entry ["orphans"] = int(orphans);

                if (closed > 0)
                {
                    int const percent (
                        ((seen - missing) * 100) / closed);
                    entry ["percent"] = percent;
                }

                entries.append (entry);
            }
        }
        result ["validators"] = entries;

#endif
        return result;
    }

    // Returns the list of sources
    Json::Value rpcSources (Json::Value const& arg)
    {
        Json::Value result (Json::objectValue);

        Json::Value entries (Json::arrayValue);
        SharedState::ConstAccess state (m_state);
        for (SourcesType::const_iterator iter (state->sources.begin());
            iter != state->sources.end(); ++iter)
        {
            Json::Value entry (Json::objectValue);
            SourceDesc const& desc (*iter);
            entry ["name"] = desc.source->name();
            entry ["param"] = desc.source->createParam();

            Json::Value results (Json::arrayValue);
            for (int i = 0; i < desc.result.list.size(); ++i)
            {
                Json::Value info (Json::objectValue);
                info ["key"] = "publicKey";
                info ["label"] = desc.result.list[i].label;
                results.append (info);
            }
            entry ["result"] = results;

            entries.append (entry);
        }
        result ["sources"] = entries;

        return result;
    }

    //----------------------------------------------------------------------
    // 
    // Ripple interface
    //

    // VFALCO NOTE We cannot make any assumptions about the quality of the
    //             information being passed into the logic. Specifically,
    //             we can expect to see duplicate ledgerClose, and duplicate
    //             receiveValidation. Therefore, we must program defensively
    //             to prevent undefined behavior

    // Called when we receive a signed validation
    //
    // Used to filter duplicate public keys
    //
    typedef AgedCache <RipplePublicKey, 
    typedef AgedHistory <boost::unordered_set <
        RipplePublicKey, RipplePublicKey::hasher> > SeenPublicKeys;
    SeenPublicKeys m_seenPublicKeys;

    void receiveValidation (ReceivedValidation const& rv)
    {
        // Filter duplicates
        {
            std::pair <SeenPublicKeys::container_type::iterator, bool> result (
                    m_seenPublicKeys->emplace (rv.publicKey));
            if (m_seenPublicKeys->size() > maxSizeBeforeSwap)
            {
                m_seenPublicKeys.swap();
                m_seenPublicKeys->clear();
            }
            if (! result.second)
                return;
        }

        SharedState::Access state (m_state);
#if 1
        // Accept validation from the trusted list
        ValidatorMap::iterator iter (state->validators.find (rv.publicKey));
        if (iter != state->validators.end ())
        {
            Validator& v (iter->second);
            v.receiveValidation (rv.ledgerHash);
        }
#else
        // Accept any validation (for testing)
        std::pair <ValidatorMap::iterator, bool> result (
            state->validators.emplace (rv.publicKey, Validator()));
        Validator& v (result.first->second);
        v.receiveValidation (rv.ledgerHash);
#endif
    }

    // Called when a ledger is closed
    //
    void ledgerClosed (RippleLedgerHash const& ledgerHash)
    {
        // Filter duplicates
        {
            std::pair <SeenLedgerHashes::container_type::iterator, bool> result (
                    m_seenLedgerHashes->emplace (ledgerHash));
            if (m_seenLedgerHashes->size() > maxSizeBeforeSwap)
            {
                m_seenLedgerHashes.swap();
                m_seenLedgerHashes->clear();
            }
            if (! result.second)
                return;
        }

        SharedState::Access state (m_state);
        for (ValidatorMap::iterator iter (state->validators.begin());
            iter != state->validators.end(); ++iter)
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

    //
    //----------------------------------------------------------------------
};

}
}

#endif
