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

// Tunable constants
enum
{
#if 0
    // We will fetch a source at this interval
    hoursBetweenFetches = 24
    ,secondsBetweenFetches = hoursBetweenFetches * 60 * 60
    // We check Source expirations on this time interval
    ,checkEverySeconds = 60 * 60
#else
     secondsBetweenFetches = 59
    ,checkEverySeconds = 60
#endif

    // This tunes the preallocated arrays
    ,expectedNumberOfResults = 1000
};

//------------------------------------------------------------------------------

enum
{
    maxSizeBeforeSwap    = 100
};

//------------------------------------------------------------------------------

struct Ledger
{
    Ledger() : when (Time::getCurrentTime())
    {
    }

    Time when;
};

typedef AgedHistory <boost::unordered_map <
    RippleLedgerHash, Ledger, RippleLedgerHash::hasher> > Ledgers;

// Information associated with each distinguishable validator
struct Validator
{
    Validator ()
        : refCount (0)
    {
    }

    void receiveValidation (RippleLedgerHash const& ledgerHash)
    {
        typedef Ledgers::container_type::iterator iterator;

        ++count->seen;

        // If we already have it in the expected list, close it out
        //
        iterator iter (expected->find (ledgerHash));
        if (iter != expected->end())
        {
            expected->erase (iter);
            expected.back().erase (ledgerHash);
            return;
        }
        else if ((iter = expected.back().find(ledgerHash)) !=
            expected.back().end())
        {
            expected.back().erase (iter);
            return;
        }

        // Ledger hasn't closed yet so put it in the received list
        //
        std::pair <iterator, bool> result (
            received->emplace (ledgerHash, Ledger()));
        bassert (result.second);
        if (received->size() >= maxSizeBeforeSwap)
            swap();
    }

    void ledgerClosed (RippleLedgerHash const& ledgerHash)
    {
        typedef Ledgers::container_type::iterator iterator;

        ++count->closed;

        // If the Validator already gave us the ledger
        // then count it and remove it from both tables.
        //
        iterator iter (received->find (ledgerHash));
        if (iter != received->end())
        {
            received->erase (iter);
            received.back().erase (ledgerHash);
            return;
        }
        else if ((iter = received.back().find (ledgerHash)) !=
            received.back().end())
        {
            received.back().erase (iter);
            return;
        }

        // We haven't seen this ledger hash from the
        // validator yet so put it on the expected list
        //
        std::pair <iterator, bool> result (
            expected->emplace (ledgerHash, Ledger ()));
        bassert (result.second);
        if (expected->size() >= maxSizeBeforeSwap)
            swap();
    }

    void swap()
    {
        // Count anything in the old expected list as missing
        count->missing += expected.back().size();

        // Count anything in the old received list as orphaned
        count->orphans += received.back().size();

        // Rotate and clear
        count.swap();
        expected.swap();
        received.swap();
        count->clear();
        expected->clear();
        received->clear();
    }

    struct Count
    {
        Count()
            : closed (0)
            , seen (0)
            , missing (0)
            , orphans (0)
        {
        }

        void clear ()
        {
            *this = Count();
        }

        // How many ledgers we've seen
        std::size_t closed;

        // How many validation's we've seen
        std::size_t seen;

        // Estimate of validation's that were missed
        std::size_t missing;

        // Estimate of validations not belonging to any ledger
        std::size_t orphans;
    };

    int refCount;

    AgedHistory <Count> count;
    Ledgers received;
    Ledgers expected;
};

//------------------------------------------------------------------------------

// Encapsulates the logic for creating the chosen validators.
// This is a separate class to facilitate the unit tests.
//
class Logic
{
public:
    //--------------------------------------------------------------------------

    typedef boost::unordered_map <
        RipplePublicKey, Validator,
            RipplePublicKey::hasher> MapType;

    // The master in-memory database of Validator, indexed by all the
    // possible things that we need to care about, and even some that we don't.
    //
    /*
    typedef boost::multi_index_container <
        Validator, boost::multi_index::indexed_by <
            
            boost::multi_index::hashed_unique <
                BOOST_MULTI_INDEX_MEMBER(Logic::Validator,UniqueID,uniqueID)>,

            boost::multi_index::hashed_unique <
                BOOST_MULTI_INDEX_MEMBER(Logic::Validator,IPEndpoint,endpoint),
                Connectible::HashAddress>
        >
    > ValidationsMap;
    */

    //--------------------------------------------------------------------------

    struct State
    {
        State ()
        {
            //sources.reserve (64);
        }

        MapType map;
        SourcesType sources;
    };

    typedef SharedData <State> SharedState;

    Store& m_store;
    Journal m_journal;
    bool m_rebuildChosenList;
    ChosenList::Ptr m_chosenList;
    SharedState m_state;

    // Used to filter duplicate public keys
    //
    typedef AgedHistory <boost::unordered_set <
        RipplePublicKey, RipplePublicKey::hasher> > SeenPublicKeys;
    SeenPublicKeys m_seenPublicKeys;

    // Used to filter duplicate ledger hashes
    //
    typedef AgedHistory <boost::unordered_set <
        RippleLedgerHash, RippleLedgerHash::hasher> > SeenLedgerHashes;
    SeenLedgerHashes m_seenLedgerHashes;

    //----------------------------------------------------------------------

    Logic (Store& store, Journal journal = Journal ())
        : m_store (store)
        , m_journal (journal)
        , m_rebuildChosenList (false)
    {
    }

    void load ()
    {
        // load data from m_store
    }

    // Add a one-time static source.
    // Fetch is called right away, this call blocks.
    //
    void addStatic (Source* source)
    {
        m_journal.info << "Addding static source '" << source->name() << "'";

        ScopedPointer <Source> object (source);
        Source::Result result (object->fetch (m_journal));

        if (result.success)
        {
            SharedState::Access state (m_state);
            merge (result.list, source, state);
        }
        else
        {
            // TODO: Report the error
        }
    }

    // Add a live source to the list of sources.
    //
    void add (Source* source)
    {
        m_journal.info << "Adding source '" << source->name() << "'";

        {
            SharedState::Access state (m_state);
            state->sources.resize (state->sources.size() + 1);
            SourceDesc& desc (state->sources.back());
            desc.source = source;
            m_store.insert (desc);
        }
    }

    // Add each entry in the list to the map, incrementing the
    // reference count if it already exists, and updating fields.
    //
    void merge (Array <Source::Info> const& list,
        Source* source, SharedState::Access& state)
    {
        std::size_t numAdded (0);
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list.getReference (i));
            std::pair <MapType::iterator, bool> result (
                state->map.emplace (info.publicKey, Validator ()));
            Validator& validatorInfo (result.first->second);
            ++validatorInfo.refCount;
            if (result.second)
            {
                // This is a new one
                ++numAdded;
                dirtyChosen ();
            }
        }

        m_journal.info << "Added " << numAdded
                       << " trusted validators from '" << source->name() << "'";
    }

    // Decrement the reference count of each item in the list
    // in the map.
    //
    void remove (Array <Source::Info> const& list,
        Source* source, SharedState::Access& state)
    {
        std::size_t numRemoved (0);
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list.getReference (i));
            MapType::iterator iter (state->map.find (info.publicKey));
            bassert (iter != state->map.end ());
            Validator& validatorInfo (iter->second);
            if (--validatorInfo.refCount == 0)
            {
                // Last reference removed
                ++numRemoved;
                state->map.erase (iter);
                dirtyChosen ();
            }
        }

        m_journal.info << "Removed " << numRemoved
                       << " trusted validators from '" << source->name() << "'";
    }

    //----------------------------------------------------------------------
    //
    // Chosen
    //

    /** Rebuild the Chosen List. */
    void buildChosen ()
    {
        SharedState::ConstAccess state (m_state);
        ChosenList::Ptr list (new ChosenList (state->map.size ()));

        for (MapType::const_iterator iter = state->map.begin ();
            iter != state->map.end (); ++iter)
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
        m_journal.info << "fetch " << desc.source->name();

        Source::Result result (desc.source->fetch (m_journal));

        // Reset fetch timer for the source.
        desc.whenToFetch = Time::getCurrentTime () +
            RelativeTime (secondsBetweenFetches);

        if (result.success)
        {
            SharedState::Access state (m_state);

            // Add the new source info to the map
            merge (result.list, desc.source, state);

            // Swap lists
            desc.result.swapWith (result);

            // Remove the old source info from the map
            remove (result.list, desc.source, state);

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
            for (ChosenList::MapType::const_iterator iter (list->map().begin());
                iter != list->map().end(); ++iter)
            {
                Json::Value entry (Json::objectValue);
                ChosenList::MapType::key_type const& key (iter->first);
                entry ["key"] = key.to_string();
                //ChosenList::MapType::mapped_type const& value (iter->second);
                //entry ["value"] = value.to_string();
                entries.append (entry);
            }
        }
        result ["chosen_list"] = entries;

       {
            SharedState::ConstAccess state (m_state);
            std::size_t count (0);
            result ["validators"] = state->map.size();
            for (MapType::const_iterator iter (state->map.begin());
                iter != state->map.end(); ++iter)
                count += iter->second.map.size();
            result ["signatures"] = count;
        }
#else
        Json::Value entries (Json::arrayValue);
        {
            SharedState::ConstAccess state (m_state);
            result ["count"] = int(state->map.size());
            for (MapType::const_iterator iter (state->map.begin());
                iter != state->map.end(); ++iter)
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
#if 0
        MapType::iterator iter (state->map.find (rv.publicKey));
        if (iter != state->map.end ())
        {
            Validator& v (iter->second);
            v.receiveValidation (rv.ledgerHash);
        }
#else
        std::pair <MapType::iterator, bool> result (
            state->map.emplace (rv.publicKey, Validator()));
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
        for (MapType::iterator iter (state->map.begin());
            iter != state->map.end(); ++iter)
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
