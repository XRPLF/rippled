//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/*

Goal:

  Provide the listener with a ValidatorList.
  - This forms the UNL

Task:

  fetch ValidatorInfo array from a source

  - We have the old one and the new one, compute the following:

    * unchanged validators list
    * new validators list
    * removed validators list

  - From the unchanged / new / removed, figure out what to do.

Two important questions:

- Are there any validators in my ChosenValidators that I dont want
  * For example, they have dropped off all the trusted lists

- Do I have enough?

*/

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

    struct SourceInfo : LeakChecked <SourceInfo>
    {
        enum Status
        {
            statusNone,
            statusFetched,
            statusFailed,
        };

        explicit SourceInfo (Source* source_)
            : source (source_)
            , status (statusNone)
            , numberOfFailures (0)
        {
        }

        ScopedPointer <Source> const source;
        Status status;
        Time whenToFetch;
        int numberOfFailures; // of fetch()

        Validator::List::Ptr lastFetchResults;
    };

    // This is what comes back from a source
    typedef OwnedArray <SourceInfo> SourceInfoArray;

    // The result of performing a fetch
    struct FetchResult
    {
#if 0
        // This is what comes back from the fetch
        Validator::ListImp::Ptr updatedList;

        // The original list before the fetch
        Validator::List oldList;

        // The new list after the fetch
        Validator::List newList;

        // The list of validators that were added
        Validator::List addedList;

        // The list of validators that were removed
        Validator::List removedList;
#endif

        FetchResult ()
        {
            /*
            updatedList.ensureStorageAllocated (expectedNumberOfResults);
            oldList.ensureStorageAllocated (expectedNumberOfResults);
            newList.ensureStorageAllocated (expectedNumberOfResults);
            addedList.ensureStorageAllocated (expectedNumberOfResults);
            removedList.ensureStorageAllocated (expectedNumberOfResults);
            */
        }

        void clear ()
        {
            /*
            //updatedList.clearQuick ();
            oldList.clear ();
            newList.clear ();
            addedList.clear ();
            removedList.clear ();
            */
        }
    };

    //--------------------------------------------------------------------------

    // Encapsulates the logic for creating the chosen validators.
    // This is a separate class to facilitate the unit tests.
    //
    class Logic
    {
    private:
        HashMap <Validator::PublicKey,
                 Validator::Ptr,
                 Validator::PublicKey::HashFunction> m_map;
                 
        SourceInfoArray m_sourceInfo;

    public:
        Logic ()
        {
        }

        void addSource (Source* source)
        {
            m_sourceInfo.add (new SourceInfo (source));
        }

        SourceInfoArray& getSources ()
        {
            return m_sourceInfo;
        }

        void sortValidatorInfo (Array <Validator::Info>& arrayToSort)
        {
            Array <Validator::Info> sorted;

            sorted.ensureStorageAllocated (arrayToSort.size ());

            for (int i = 0; i < arrayToSort.size (); ++i)
            {
                Validator::Info::Compare compare;
                sorted.addSorted (compare, arrayToSort [i]);
            }

            arrayToSort.swapWithArray (sorted);
        }

        // Given the old list and the new list for a source, this
        // computes which validators were added or removed, and
        // updates some statistics. It also produces the new list.
        //
        void processFetch (FetchResult* pFetchResult,
                           Validator::List& oldList,
                           Validator::List& newList)
        {
#if 0
            ValidatorsImp::FetchResult& fetchResult (*pFetchResult);

            // First sort both arrays.
            //
            Validator::Info::Compare compare;
            oldList.sort (compare, true);
            newList.sort (compare, true);

            // Now walk both arrays and do an element-wise
            // comparison to determine the set intersection.
            //
            for (int i = 0; i < bmax (oldList.size (), newList.size ()); ++i)
            {
                if (i >= oldList.size ())
                {
                    // newList [i] not present in oldList
                    // newList [i] was added
                }
                else if (i >= newList.size ())
                {
                    // oldList [i] not present in newList
                    // oldList [i] no longer present
                }
                else
                {
                    int const result = Validator::Info::Compare::compareElements (
                        oldList [i], newList [i]);

                    if (result < 0)
                    {
                        // oldList [i] not present in newList
                        // oldList [i] was removed
                    }
                    else if (result > 0)
                    {
                        // newList [i] not present in oldList
                        // newList [i] was added
                    }
                    else
                    {
                        // no change in validator
                    }
                }
            }
#endif
        }

        // Produces an array of references to validators given the validator info.
        Validator::List::Ptr createListFromInfo (Array <Validator::Info> const& info)
        {
            SharedObjectArray <Validator> items;

            items.ensureStorageAllocated (info.size ());

            for (int i = 0; i < info.size (); ++i)
            {
                Validator::PublicKey const& key (info [i].publicKey);

                Validator::Ptr validator = m_map [key];

                if (validator == nullptr)
                {
                    validator = new Validator (key);

                    m_map [key] = validator;
                }

                items.add (validator);
            }

            return new Validator::List (items);
        }

        // Fetch the validators from a source and process the result
        //
        void fetchSource (SourceInfo& sourceInfo)
        {
            Array <Validator::Info> fetchedInfo = sourceInfo.source->fetch ();

            if (fetchedInfo.size () != 0)
            {
                sourceInfo.status = SourceInfo::statusFetched;

                sourceInfo.whenToFetch = Time::getCurrentTime () +
                    RelativeTime (hoursBetweenFetches * 60.0 * 60.0);

                //processFetchedInfo (fetchedInfo);
            }
            else
            {
                // Failed to fetch
                // Don't update fetch time
                sourceInfo.status = SourceInfo::statusFailed;
                sourceInfo.numberOfFailures++;
            }
        }

    };

    //--------------------------------------------------------------------------

public:
    explicit ValidatorsImp (Validators::Listener* listener)
        : m_listener (listener)
        , m_thread ("Validators")
        , m_timer (this)
    {
        m_thread.start (this);
    }

    ~ValidatorsImp ()
    {
    }

    void addSource (Source* source)
    {
        m_thread.call (&ValidatorsImp::doAddSource, this, source);
    }

    void doAddSource (Source* source)
    {
        m_logic.addSource (source);
    }

    void onDeadlineTimer (DeadlineTimer&)
    {
        // This will make us fall into the idle proc as needed
        //
        m_thread.interrupt ();
    }

    // Fetch sources whose deadline timers have arrived.
    //
    bool scanSources ()
    {
        bool interrupted = false;

        for (int i = 0; i < m_logic.getSources ().size (); ++i)
        {
            SourceInfo& sourceInfo (*m_logic.getSources ()[i]);

            Time const currentTime = Time::getCurrentTime ();

            if (currentTime <= sourceInfo.whenToFetch)
            {
                m_logic.fetchSource (sourceInfo);
            }

            interrupted = m_thread.interruptionPoint ();

            if (interrupted)
                break;
        }

        return interrupted;
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

        interrupted = scanSources ();

        return interrupted;
    }

private:
    Logic m_logic;
    Validators::Listener* const m_listener;
    ThreadWithCallQueue m_thread;
    DeadlineTimer m_timer;
};

Validators* Validators::New (Listener* listener)
{
    return new ValidatorsImp (listener);
}

//------------------------------------------------------------------------------

class ValidatorsTests : public UnitTest
{
public:
    // Produces validators for unit tests.
    class TestSource : public Validators::Source
    {
    public:
        TestSource (unsigned int startIndex, unsigned int endIndex)
            : m_startIndex (startIndex)
            , m_endIndex (endIndex)
        {
        }

        Array <Validator::Info> fetch ()
        {
            Array <Validator::Info> results;

            for (unsigned int publicKeyIndex = m_startIndex; publicKeyIndex <= m_endIndex; ++publicKeyIndex)
            {
                Validator::Info info;

                info.publicKey = Validator::PublicKey::createFromInteger (publicKeyIndex);

                results.add (info);
            }

            return results;
        }

    private:
        unsigned int const m_startIndex;
        unsigned int const m_endIndex;
    };

    //--------------------------------------------------------------------------

    ValidatorsTests () : UnitTest ("Validators", "ripple")
    {
    }

    // Check logic for comparing a source's fetch results
    void processTest ()
    {
        beginTestCase ("process");

        {
            Array <Validator::Info> results = TestSource (1, 32).fetch ();
            expect (results.size () == 32);
        }

        {
            Array <Validator::Info> oldList = TestSource (1, 2).fetch ();
            expect (oldList.size () == 2);

            Array <Validator::Info> newList = TestSource (2, 3).fetch ();
            expect (newList.size () == 2);

            ValidatorsImp::Logic logic;

            Validator::List::Ptr list = logic.createListFromInfo (newList);

            //ValidatorsImp::FetchResult fetchResult;
            //ValidatorsImp::Logic ().processFetch (&fetchResult, oldList, newList);
        }
    }

    void runTest ()
    {
        processTest ();
    }
};

static ValidatorsTests validatorsTests;

