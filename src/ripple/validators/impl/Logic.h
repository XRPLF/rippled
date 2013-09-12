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

// Encapsulates the logic for creating the chosen validators.
// This is a separate class to facilitate the unit tests.
//
class Logic
{
public:
    // Information associated with each Source
    //
    struct SourceDesc
    {
        enum
        {
            keysPreallocationSize = 1000
        };

        enum Status
        {
            statusNone,
            statusFetched,
            statusFailed
        };

        ScopedPointer <Source> source;
        Status status;
        Time whenToFetch;
        int numberOfFailures;

        // The result of the last fetch
        Source::Result result;

        //------------------------------------------------------------------

        SourceDesc () noexcept
            : status (statusNone)
            , whenToFetch (Time::getCurrentTime ())
            , numberOfFailures (0)
        {
        }

        ~SourceDesc ()
        {
        }
    };

    typedef DynamicList <SourceDesc> SourcesType;

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

    typedef boost::unordered_map <PublicKey, ValidatorInfo, PublicKey::HashFunction> MapType;

    //----------------------------------------------------------------------

    explicit Logic (Journal journal = Journal ())
        : m_journal (journal)
        , m_chosenListNeedsUpdate (false)
    {
    }

    // Add a one-time static source.
    // Fetch is called right away, this call blocks.
    //
    void addStaticSource (Source* source)
    {
        m_journal.info() << "Add static Source, " << source->name();

        ScopedPointer <Source> object (source);

        NoOpCancelCallback cancelCallback;

        Source::Result result (object->fetch (cancelCallback, m_journal));

        if (result.success)
        {
            addSourceInfo (result.list);
        }
        else
        {
            // VFALCO NOTE Maybe log the error and message?
        }
    }

    // Add a live source to the list of sources.
    //
    void addSource (Source* source)
    {
        m_journal.info() << "Add Source, " << source->name();

        SourceDesc& desc (*m_sources.emplace_back ());
        desc.source = source;
    }

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

    // Add each entry in the list to the map, incrementing the
    // reference count if it already exists, and updating fields.
    //
    void addSourceInfo (Array <Source::Info> const& list)
    {
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list.getReference (i));
            std::pair <MapType::iterator, bool> result (
                m_map.emplace (info.key, ValidatorInfo ()));
            ValidatorInfo& validatorInfo (result.first->second);
            ++validatorInfo.refCount;
            if (result.second)
            {
                // This is a new one
                markDirtyChosenList ();
            }
        }
    }

    // Decrement the reference count of each item in the list
    // in the map
    //
    void removeSourceInfo (Array <Source::Info> const& list)
    {
        for (std::size_t i = 0; i < list.size (); ++i)
        {
            Source::Info const& info (list.getReference (i));
            MapType::iterator iter (m_map.find (info.key));
            bassert (iter != m_map.end ());
            ValidatorInfo& validatorInfo (iter->second);
            if (--validatorInfo.refCount == 0)
            {
                // Last reference removed
                m_map.erase (iter);
                markDirtyChosenList ();
            }
        }
    }

    // Fetch one source
    //
    void fetchSource (SourceDesc& desc, Source::CancelCallback& callback)
    {
        m_journal.info() << "Fetching Source, " << desc.source->name();

        Source::Result result (desc.source->fetch (callback, m_journal));

        if (! callback.shouldCancel ())
        {
            // Reset fetch timer for the source.
            desc.whenToFetch = Time::getCurrentTime () +
                RelativeTime (secondsBetweenFetches);

            if (result.success)
            {
                // Add the new source info to the map
                addSourceInfo (result.list);

                // Swap lists
                desc.result.swapWith (result);

                // Remove the old source info from the map
                removeSourceInfo (result.list);

                // See if we need to rebuild
                checkDirtyChosenList ();

                // Reset failure status
                desc.numberOfFailures = 0;
                desc.status = SourceDesc::statusFetched;
            }
            else
            {
                ++desc.numberOfFailures;
                desc.status = SourceDesc::statusFailed;
            }
        }
    }

    // Check each source to see if it needs fetching.
    //
    void checkSources (Source::CancelCallback& callback)
    {
        m_journal.info() << "Checking Sources";

        Time const currentTime (Time::getCurrentTime ());
        for (SourcesType::iterator iter = m_sources.begin ();
                ! callback.shouldCancel () && iter != m_sources.end (); ++iter)
        {
            SourceDesc& desc (*iter);
            if (desc.whenToFetch <= currentTime)
                fetchSource (desc, callback);
        }
    }

    // Signal that the Chosen List needs to be rebuilt.
    //
    void markDirtyChosenList ()
    {
        m_chosenListNeedsUpdate = true;
    }

    // Check the dirty state of the Chosen List, and rebuild it
    // if necessary.
    //
    void checkDirtyChosenList ()
    {
        if (m_chosenListNeedsUpdate)
        {
            buildChosenList ();
            m_chosenListNeedsUpdate = false;
        }
    }

    // Rebuilds the Chosen List
    //
    void buildChosenList ()
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

        m_journal.debug() <<
            "Rebuilt chosen list with " <<
            String::fromNumber (m_chosenList->size()) << " entries";
    }

    // Get a reference to the chosen list.
    // This is safe to call from any thread at any time.
    //
    ChosenList::Ptr getChosenList ()
    {
        return m_chosenList;
    }

    //----------------------------------------------------------------------
    // 
    // Ripple interface
    //
    // These routines are modeled after UniqueNodeList

    bool isTrustedPublicKeyHash (
        PublicKeyHash const& publicKeyHash)
    {
        return m_chosenList->containsPublicKeyHash (publicKeyHash);
    }

    //
    //
    //----------------------------------------------------------------------

private:
    Journal m_journal;
    SourcesType m_sources;
    MapType m_map;
    bool m_chosenListNeedsUpdate;
    ChosenList::Ptr m_chosenList;
};

}

#endif
