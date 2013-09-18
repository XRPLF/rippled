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
    //----------------------------------------------------------------------

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

        if (result.success)
        {
            merge (result.list);
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

        SourceDesc& desc (*m_sources.emplace_back ());
        desc.source = source;

        m_store.insert (desc);
    }

    // Add each entry in the list to the map, incrementing the
    // reference count if it already exists, and updating fields.
    //
    void merge (Array <Source::Info> const& list)
    {
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list.getReference (i));
            std::pair <MapType::iterator, bool> result (
                m_map.emplace (info.publicKey, ValidatorInfo ()));
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
    void remove (Array <Source::Info> const& list)
    {
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list.getReference (i));
            MapType::iterator iter (m_map.find (info.publicKey));
            bassert (iter != m_map.end ());
            ValidatorInfo& validatorInfo (iter->second);
            if (--validatorInfo.refCount == 0)
            {
                // Last reference removed
                m_map.erase (iter);
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
        ChosenList::Ptr list (new ChosenList (m_map.size ()));

        for (MapType::iterator iter = m_map.begin ();
            iter != m_map.end (); ++iter)
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
                // Add the new source info to the map
                merge (result.list);

                // Swap lists
                desc.result.swapWith (result);

                // Remove the old source info from the map
                remove (result.list);

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
    void expire (SourceDesc& desc)
    {
        // Decrement reference count on each validator
        remove (desc.result.list);

        m_store.update (desc);
    }

    /** Check each Source to see if it needs processing.
        @return `true` if an interruption occurred.
    */
    bool check (CancelCallback& callback)
    {
        bool interrupted (false);
        Time const currentTime (Time::getCurrentTime ());
        for (SourcesType::iterator iter = m_sources.begin ();
            iter != m_sources.end (); ++iter)
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
                expire (desc);
            }
        }

        return interrupted;
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
        MapType::iterator iter (m_map.find (rv.signerPublicKeyHash));
        if (iter != m_map.end ())
        {
            // Exists
            //ValidatorInfo& validatorInfo (iter->value ());
        }
        else
        {
            // New
            //ValidatorInfo& validatorInfo (m_map.insert (rv.signerPublicKeyHash));
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

private:
    Store& m_store;
    Journal m_journal;
    SourcesType m_sources;
    MapType m_map;
    bool m_rebuildChosenList;
    ChosenList::Ptr m_chosenList;
};

}

#endif
