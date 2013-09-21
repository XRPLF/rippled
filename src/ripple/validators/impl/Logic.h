//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_LOGIC_H_INCLUDED
#define RIPPLE_VALIDATORS_LOGIC_H_INCLUDED

namespace Validators
{

// Tunable constants
enum
{
    // We will fetch a source at this interval
    hoursBetweenFetches = 24

    ,secondsBetweenFetches = hoursBetweenFetches * 60 * 60

    // We check Source expirations on this time interval
    ,checkEverySeconds = 60 * 60

    // This tunes the preallocated arrays
    ,expectedNumberOfResults = 1000
};

//------------------------------------------------------------------------------

// Encapsulates the logic for creating the chosen validators.
// This is a separate class to facilitate the unit tests.
//
class Logic
{
public:
    // Information associated with each distinguishable validator
    //
    struct ValidatorInfo
    {
        ValidatorInfo ()
            : refCount (0)
        {
        }

        int refCount;
    };

    typedef boost::unordered_map <
        PublicKey, ValidatorInfo, PublicKey::HashFunction> MapType;

    struct State
    {
        MapType map;
        SourcesType sources;
    };

    typedef SharedData <State> SharedState;

    Store& m_store;
    Journal m_journal;
    bool m_rebuildChosenList;
    ChosenList::Ptr m_chosenList;
    SharedState m_state;

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
        m_journal.info << "Add static Source, " << source->name();

        ScopedPointer <Source> object (source);

        NoOpCancelCallback cancelCallback;

        Source::Result result (object->fetch (cancelCallback, m_journal));

        SharedState::Access state (m_state);
        if (result.success)
        {
            merge (result.list, state);
        }
        else
        {
            // VFALCO NOTE Maybe log the error and message?
        }
    }

    // Add a live source to the list of sources.
    //
    void add (Source* source)
    {
        m_journal.info << "Add Source, " << source->name();
        SharedState::Access state (m_state);
        SourceDesc& desc (*state->sources.emplace_back ());
        desc.source = source;
        m_store.insert (desc);
    }

    // Add each entry in the list to the map, incrementing the
    // reference count if it already exists, and updating fields.
    //
    void merge (Array <Source::Info> const& list, SharedState::Access& state)
    {
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list.getReference (i));
            std::pair <MapType::iterator, bool> result (
                state->map.emplace (info.publicKey, ValidatorInfo ()));
            ValidatorInfo& validatorInfo (result.first->second);
            ++validatorInfo.refCount;
            if (result.second)
            {
                // This is a new one
                dirtyChosen ();
            }
        }
    }

    // Decrement the reference count of each item in the list
    // in the map.
    //
    void remove (Array <Source::Info> const& list, SharedState::Access& state)
    {
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list.getReference (i));
            MapType::iterator iter (state->map.find (info.publicKey));
            bassert (iter != state->map.end ());
            ValidatorInfo& validatorInfo (iter->second);
            if (--validatorInfo.refCount == 0)
            {
                // Last reference removed
                state->map.erase (iter);
                dirtyChosen ();
            }
        }
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
    void fetch (SourceDesc& desc, CancelCallback& callback)
    {
        m_journal.info << "fetch ('" << desc.source->name() << "')";

        Source::Result result (desc.source->fetch (callback, m_journal));

        if (! callback.shouldCancel ())
        {
            // Reset fetch timer for the source.
            desc.whenToFetch = Time::getCurrentTime () +
                RelativeTime (secondsBetweenFetches);

            if (result.success)
            {
                SharedState::Access state (m_state);

                // Add the new source info to the map
                merge (result.list, state);

                // Swap lists
                desc.result.swapWith (result);

                // Remove the old source info from the map
                remove (result.list, state);

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
    }

    /** Expire a source's list of validators. */
    void expire (SourceDesc& desc, SharedState::Access& state)
    {
        // Decrement reference count on each validator
        remove (desc.result.list, state);

        m_store.update (desc);
    }

    /** Check each Source to see if it needs processing.
        @return `true` if an interruption occurred.
    */
    bool check (CancelCallback& callback)
    {
        bool interrupted (false);
        Time const currentTime (Time::getCurrentTime ());
        
        SharedState::Access state (m_state);
        for (SourcesType::iterator iter = state->sources.begin ();
            iter != state->sources.end (); ++iter)
        {
            SourceDesc& desc (*iter);

            // See if we should fetch
            //
            if (desc.whenToFetch <= currentTime)
            {
                fetch (desc, callback);
                if (callback.shouldCancel ())
                {
                    interrupted = true;
                    break;
                }
            }

            // See if we need to expire
            //
            if (desc.expirationTime.isNotNull () &&
                desc.expirationTime <= currentTime)
            {
                expire (desc, state);
            }
        }

        return interrupted;
    }

    //----------------------------------------------------------------------
    // 
    // RPC Handlers
    //

    // Return the current ChosenList as JSON
    Json::Value rpcPrint (Json::Value const& args)
    {
        Json::Value result;
        ChosenList::Ptr list (m_chosenList);

        if (! list.empty())
        {
            Json::Value entries (result["chosen_list"]);
            std::size_t i (1);
            for (ChosenList::MapType::const_iterator iter (list->map().begin());
                iter != list->map().end(); ++iter)
            {
                ChosenList::MapType::key_type const& key (iter->first);
                ChosenList::MapType::mapped_type const& value (iter->second);
                entries[i] = i;
                ++i;
            }
        }
        else
        {
            result ["chosen_list"] = "empty";
        }

        return result;
    }

    // Returns the list of sources
    Json::Value rpcSources (Json::Value const& arg)
    {
        Json::Value result;
        Json::Value sources (result ["validators_sources"]);

        return result;
    }

    //----------------------------------------------------------------------
    // 
    // Ripple interface
    //

    // Called when we receive a validation from a peer.
    //
    void receiveValidation (ReceivedValidation const& rv)
    {
#if 0
        MapType::iterator iter (state->map.find (rv.signerPublicKeyHash));
        if (iter != state->map.end ())
        {
            // Exists
            //ValidatorInfo& validatorInfo (iter->value ());
        }
        else
        {
            // New
            //ValidatorInfo& validatorInfo (state->map.insert (rv.signerPublicKeyHash));
        }
#endif
    }

    // Returns `true` if the public key hash is contained in the Chosen List.
    //
    bool isTrustedPublicKeyHash (PublicKeyHash const& publicKeyHash)
    {
        return m_chosenList->containsPublicKeyHash (publicKeyHash);
    }

    //
    //
    //----------------------------------------------------------------------
};

}

#endif
