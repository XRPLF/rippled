//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_VALIDATOR_VALIDATORSIMP_H_INCLUDED
#define RIPPLE_CORE_VALIDATOR_VALIDATORSIMP_H_INCLUDED

// private implementation

class ValidatorsImp
    : public Validators
    , private ThreadWithCallQueue::EntryPoints
    , private DeadlineTimer::Listener
    , LeakChecked <ValidatorsImp>
{
public:
    // Tunable constants
    enum
    {
        // We will fetch a source at this interval
        hoursBetweenFetches = 24

        ,secondsBetweenFetches = hoursBetweenFetches * 60 * 60

        // Wake up every hour to check source times
        ,secondsPerUpdate = 60 * 60

        // This tunes the preallocated arrays
        ,expectedNumberOfResults = 1000
    };

    //--------------------------------------------------------------------------

    // Dummy CancelCallback that does nothing
    //
    struct NoOpCancelCallback : Source::CancelCallback
    {
        bool shouldCancel ()
        {
            return false;
        }

    };
            
    //--------------------------------------------------------------------------

    /** Receive event notifications on Validators operations.
    */
    class Listener
    {
    public:
    };

    //--------------------------------------------------------------------------

    class ChosenList : public SharedObject
    {
    public:
        typedef SharedPtr <ChosenList> Ptr;
        
        struct Info
        {
            Info ()
            {
            }
        };

        typedef HashMap <KeyType, Info, KeyType::HashFunction> MapType;

        ChosenList ()
        {
        }

        std::size_t size () const noexcept
        {
            return m_map.size ();
        }

        void insert (KeyType const& key, Info const& info) noexcept
        {
            m_map [key] = info;
        }

        bool contains (KeyType const& key) const noexcept
        {
            return m_map.find (key) != m_map.cend ();
        }

    private:
        MapType m_map;
    };

    //--------------------------------------------------------------------------

    // Encapsulates the logic for creating the chosen validators.
    // This is a separate class to facilitate the unit tests.
    //
    class Logic
    {
    public:
        // Information associated with each Source
        //
        struct SourceInfo
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

            // The result of hte last fetch
            Array <Source::Info> list;

            //------------------------------------------------------------------

            SourceInfo () noexcept
                : status (statusNone)
                , whenToFetch (Time::getCurrentTime ())
                , numberOfFailures (0)
            {
                list.ensureStorageAllocated (keysPreallocationSize);
            }

            ~SourceInfo ()
            {
            }
        };

        typedef DynamicList <SourceInfo> SourcesType;

        //----------------------------------------------------------------------

        // Information associated with each distinguishable validator
        //
        struct ValidatorInfo
        {
            ValidatorInfo ()
                : refCount (0)
            {
            }

            KeyType key;
            int refCount;
        };

        typedef HashMap <KeyType, ValidatorInfo, KeyType::HashFunction> MapType;

        //----------------------------------------------------------------------

        Logic ()
            : m_chosenListNeedsUpdate (false)
        {
        }

        // Add a live source to the list of sources.
        //
        void addSource (Source* source)
        {
            SourceInfo& info (*m_sources.emplace_back ());
            info.source = source;
        }

        // Add a one-time static source.
        // Fetch is called right away, this call blocks.
        //
        void addStaticSource (Source* source)
        {
            ScopedPointer <Source> object (source);

            NoOpCancelCallback cancelCallback;
            
            Array <Source::Info> list (object->fetch (cancelCallback));

            addSourceInfo (list);
        }

        // Called when we receive a validation from a peer.
        //
        void receiveValidation (Validators::ReceivedValidation const& rv)
        {
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
        }

        // Add each entry in the list to the map, incrementing the
        // reference count if it already exists, and updating fields.
        //
        void addSourceInfo (Array <Source::Info> const& list)
        {
            for (std::size_t i = 0; i < list.size (); ++i)
            {
                Source::Info const& info (list.getReference (i));
                MapType::Result result (m_map.insert (info.key));
                ValidatorInfo& validatorInfo (result.iter->value ());
                ++validatorInfo.refCount;
                if (result.inserted)
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
                ValidatorInfo& validatorInfo (iter->value ());
                if (--validatorInfo.refCount == 0)
                {
                    // Last reference removed
                    m_map.erase (info.key);
                    markDirtyChosenList ();
                }
            }
        }

        // Fetch one source
        //
        void fetchSource (SourceInfo& info, Source::CancelCallback& callback)
        {
            Array <Source::Info> list (info.source->fetch (callback));
            if (! callback.shouldCancel ())
            {
                // Reset fetch timer for the source.
                info.whenToFetch = Time::getCurrentTime () +
                    RelativeTime (secondsBetweenFetches);

                // Add the new source info to the map
                addSourceInfo (list);

                // Swap lists
                info.list.swapWith (list);

                // Remove the old source info from the map
                removeSourceInfo (list);

                // See if we need to rebuild
                checkDirtyChosenList ();
            }
        }

        // Check each source to see if it needs fetching.
        //
        void checkSources (Source::CancelCallback& callback)
        {
            Time const currentTime (Time::getCurrentTime ());
            for (SourcesType::iterator iter = m_sources.begin ();
                 ! callback.shouldCancel () && iter != m_sources.end (); ++iter)
            {
                SourceInfo& info (*iter);
                if (info.whenToFetch <= currentTime)
                    fetchSource (info, callback);
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
            ChosenList::Ptr list (new ChosenList);

            for (MapType::iterator iter = m_map.begin ();
                iter != m_map.end (); ++iter)
            {
                //ValidatorInfo const& validatorInfo (iter->value ());
                ChosenList::Info info;
                list->insert (iter->key (), info);
            }

            // This is thread safe
            m_chosenList = list;
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

        bool isTrustedPublicKeyHash (RipplePublicKeyHash const& key)
        {
            return m_chosenList->contains (key);
        }

        //
        //
        //----------------------------------------------------------------------

    private:
        SourcesType m_sources;
        MapType m_map;
        bool m_chosenListNeedsUpdate;
        ChosenList::Ptr m_chosenList;
    };

    //--------------------------------------------------------------------------

public:
    explicit ValidatorsImp (Listener* listener)
        : m_listener (listener)
        , m_thread ("Validators")
        , m_timer (this)
    {
        //m_thread.start (this);
    }

    ~ValidatorsImp ()
    {
    }

    void addSource (Source* source)
    {
        m_thread.call (&Logic::addSource, &m_logic, source);
    }

    void addStaticSource (Source* source)
    {
        m_thread.call (&Logic::addStaticSource, &m_logic, source);
    }

    void receiveValidation (ReceivedValidation const& rv)
    {
        m_thread.call (&Logic::receiveValidation, &m_logic, rv);
    }

    //--------------------------------------------------------------------------

    void onDeadlineTimer (DeadlineTimer&)
    {
        // This will make us fall into the idle proc as needed
        //
        m_thread.interrupt ();
    }

    void threadInit ()
    {
        m_timer.setRecurringExpiration (secondsPerUpdate);
    }

    void threadExit ()
    {
    }

    bool threadIdle ()
    {
        bool interrupted = false;

        struct ThreadCancelCallback : Source::CancelCallback, Uncopyable
        {
            explicit ThreadCancelCallback (ThreadWithCallQueue& thread)
                : m_thread (thread)
                , m_interrupted (false)
            {
            }

            bool shouldCancel ()
            {
                if (m_interrupted)
                    return true;
                return m_interrupted = m_thread.interruptionPoint ();
            }

        private:
            ThreadWithCallQueue& m_thread;
            bool m_interrupted;
        };

        ThreadCancelCallback cancelCallback (m_thread);

        m_logic.checkSources (cancelCallback);

        return interrupted;
    }

private:
    Logic m_logic;
    Listener* const m_listener;
    ThreadWithCallQueue m_thread;
    DeadlineTimer m_timer;
};

#endif
