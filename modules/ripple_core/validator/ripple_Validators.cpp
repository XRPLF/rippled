//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/*

Two important questions:

- Are there any validators in my ChosenValidators that I dont want
  * For example, they have dropped off all the trusted lists

- Do I have enough?

*/


class ValidatorsImp
    : public Validators
    , private ThreadWithCallQueue::EntryPoints
    , private DeadlineTimer::Listener
{
public:
    // Tunable constants
    enum
    {
        hoursBetweenFetches = 24,

        secondsBetweenFetches = hoursBetweenFetches * 60 * 60,

        timerGranularity = 60 * 60  // Wake up every hour
    };

    //--------------------------------------------------------------------------

    struct SourceInfo
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
        {
        }

        Source* source;
        Status status;
        Time lastFetch;
        ValidatorList::Ptr list;
    };

    //--------------------------------------------------------------------------

public:
    explicit ValidatorsImp (Validators::Listener* listener)
        : m_thread ("Validators")
        , m_timer (this)
        , m_listener (listener)
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
        m_sources.add (source);

        m_info.add (SourceInfo (source));
    }

    void onDeadlineTimer (DeadlineTimer&)
    {
        // This will make us fall into the idle proc as needed
        //
        m_thread.interrupt ();
    }

    void mergeValidators (ValidatorList::Ptr dest, ValidatorList::Ptr source)
    {
    }

    // Construct the list of well known validators
    ValidatorList::Ptr buildWellKnownValidators ()
    {
        ValidatorList::Ptr list = new ValidatorList;

        // Go through each source and merge its list
        for (int i = 0; i < m_info.size (); ++i)
        {
            SourceInfo const& info (m_info.getReference (i));

            if (info.status == SourceInfo::statusFetched)
            {
                mergeValidators (list, info.list);
            }
        }

        return list;
    }

    // Choose a subset of validators from the well known list
    //
    ValidatorList::Ptr chooseSubset (ValidatorList::Ptr list)
    {
        ValidatorList::Ptr result = new ValidatorList;

        return result;
    }

    // Create a composite object representing the chosen validators.
    //
    ChosenValidators::Ptr createChosenValidators ()
    {
        ValidatorList::Ptr wellKnownValidators = buildWellKnownValidators ();

        ValidatorList::Ptr validatorSubset = chooseSubset (wellKnownValidators);

        ChosenValidators::Ptr chosenValidators = new ChosenValidators (
            validatorSubset,
            wellKnownValidators);

        return chosenValidators;
    }

    // Create a fresh chosen validators from our source information
    // and broadcast it.
    //
    void updateChosenValidators ()
    {
        ChosenValidators::Ptr chosenValidators = createChosenValidators ();

        m_listener->onValidatorsChosen (chosenValidators);
    }

    // Goes through all the sources and refreshes them as needed
    //
    bool scanSources ()
    {
        bool interrupted = false;

        Time currentTime = Time::getCurrentTime ();

        // Find a source that needs to be processed
        //
        for (int i = 0; i < m_info.size (); ++i)
        {
            SourceInfo& info (m_info.getReference (i));

            // See if we need to refresh its list
            //
            if ((currentTime - info.lastFetch).inSeconds () > secondsBetweenFetches)
            {
                ValidatorList::Ptr list = info.source->fetch ();

                currentTime = Time::getCurrentTime ();

                if (list != nullptr)
                {
                    info.status = SourceInfo::statusFetched;
                    info.lastFetch = currentTime;
                    info.list = list;

                    updateChosenValidators ();
                }
            }

            interrupted = m_thread.interruptionPoint ();

            if (interrupted)
                break;
        }

        return interrupted;
    }

    void threadInit ()
    {
        m_timer.setRecurringExpiration (timerGranularity);
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
    ThreadWithCallQueue m_thread;
    DeadlineTimer m_timer;
    Validators::Listener* const m_listener;
    OwnedArray <Source> m_sources;
    Array <SourceInfo> m_info;
    ValidatorList::Ptr m_chosenValidators;
};

Validators* Validators::New (Listener* listener)
{
    return new ValidatorsImp (listener);
}
