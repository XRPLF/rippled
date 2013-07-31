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
        int numberOfFailures;
        Validator::List::Ptr list;
    };

    //--------------------------------------------------------------------------

    // Called during the list comparison
    //
    struct CompareCallback
    {
        virtual void onValidatorAdded (Validator const& validator) { }
        virtual void onValidatorRemoved (Validator const& validator) { }
        virtual void onValidatorUnchanged (Validator const& validator) { }
    };

    // Given the old list and the new list for a source, this
    // computes which validators were added or removed, and
    // updates some statistics.
    //
    static void compareLists (Validator::List const& oldList,
                              Validator::List const& newList,
                              CompareCallback& callback)
    {
        // Validator::List is always sorted so walk both arrays and
        // do an element-wise comparison to perform set calculations.
        //
        int i = 0;
        int j = 0;
        while (i < oldList.size () || j < newList.size ())
        {
            if (i < oldList.size () && j < newList.size ())
            {
                int const compare = Validator::Compare::compareElements (
                    oldList [i], newList [j]);

                if (compare < 0)
                {
                    callback.onValidatorRemoved (*oldList [i]);
                    ++i;
                }
                else if (compare > 0)
                {
                    callback.onValidatorAdded (*newList [j]);
                    ++j;
                }
                else
                {
                    bassert (oldList [i] == newList [j]);

                    callback.onValidatorUnchanged (*newList [j]);
                    ++i;
                    ++j;
                }
            }
            else if (i < oldList.size ())
            {
                callback.onValidatorRemoved (*oldList [i]);
                ++i;
            }
            else
            {
                bassert (j < newList.size ());

                callback.onValidatorAdded (*newList [j]);
                ++j;
            }
        }
    }

    // Encapsulates the logic for creating the chosen validators.
    // This is a separate class to facilitate the unit tests.
    //
    class Logic : public CompareCallback
    {
    private:
        HashMap <Validator::PublicKey,
                 Validator::Ptr,
                 Validator::PublicKey::HashFunction> m_map;
                 
        OwnedArray <SourceInfo> m_sources;

    public:
        Logic ()
        {
        }

        void addSource (Source* source)
        {
            m_sources.add (new SourceInfo (source));
        }

        OwnedArray <SourceInfo>& getSources ()
        {
            return m_sources;
        }

        void onValidatorAdded (Validator const& validator)
        {
        }

        void onValidatorRemoved (Validator const& validator)
        {
        }

        void onValidatorUnchanged (Validator const& validator)
        {
        }

        // Produces an array of references to validators given the validator info.
        //
        Validator::List::Ptr createListFromInfo (Array <Validator::Info>& info)
        {
            Validator::Info::sortAndRemoveDuplicates (info);

            SharedObjectArray <Validator> items;

            items.ensureStorageAllocated (info.size ());

            for (int i = 0; i < info.size (); ++i)
            {
                Validator::PublicKey const& key (info [i].publicKey);

                Validator::Ptr validator = m_map [key];

                if (validator == nullptr)
                {
                    validator = new Validator (key);

                    m_map.set (key, validator);
                }

                items.add (validator);
            }

            return new Validator::List (items);
        }

        // Fetch the validators from a source and process the result
        //
        void fetchAndProcessSource (SourceInfo& sourceInfo)
        {
            Array <Validator::Info> newInfo = sourceInfo.source->fetch ();

            if (newInfo.size () != 0)
            {
                sourceInfo.status = SourceInfo::statusFetched;

                sourceInfo.whenToFetch = Time::getCurrentTime () +
                    RelativeTime (hoursBetweenFetches * 60.0 * 60.0);

                Validator::List::Ptr newList (createListFromInfo (newInfo));

                compareLists (*sourceInfo.list, *newList, *this);

                sourceInfo.list = newList;
            }
            else
            {
                // Failed to fetch, don't update fetch time
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
                m_logic.fetchAndProcessSource (sourceInfo);
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

    struct TestCompareCallback : public ValidatorsImp::CompareCallback
    {
        int numTotal;
        int numAdded;
        int numRemoved;
        int numUnchanged;

        TestCompareCallback ()
            : numTotal (0)
            , numAdded (0)
            , numRemoved (0)
            , numUnchanged (0)
        {
        }

        void onValidatorAdded (Validator const& validator)
        {
            ++numTotal;
            ++numAdded;
        }

        void onValidatorRemoved (Validator const& validator)
        {
            ++numTotal;
            ++numRemoved;
        }

        void onValidatorUnchanged (Validator const& validator)
        {
            ++numTotal;
            ++numUnchanged;
        }
    };

    //--------------------------------------------------------------------------

    ValidatorsTests () : UnitTest ("Validators", "ripple", runManual)
    {
    }

    // Check logic for comparing a source's fetch results
    void testCompare ()
    {
        beginTestCase ("compare");

        {
            Array <Validator::Info> results = TestSource (1, 32).fetch ();
            expect (results.size () == 32);
        }

        {
            Array <Validator::Info> oldInfo = TestSource (1, 4).fetch ();
            expect (oldInfo.size () == 4);

            Array <Validator::Info> newInfo = TestSource (3, 6).fetch ();
            expect (newInfo.size () == 4);

            ValidatorsImp::Logic logic;

            Validator::List::Ptr oldList = logic.createListFromInfo (oldInfo);
            expect (oldList->size () == 4);

            Validator::List::Ptr newList = logic.createListFromInfo (newInfo);
            expect (newList->size () == 4);

            TestCompareCallback cb;
            ValidatorsImp::compareLists (*oldList, *newList, cb);

            expect (cb.numAdded == 2);
            expect (cb.numRemoved == 2);
            expect (cb.numUnchanged == 2);
        }
    }

    void runTest ()
    {
        testCompare ();
    }
};

static ValidatorsTests validatorsTests;

