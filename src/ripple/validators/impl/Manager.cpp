//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATORS_DISABLE_MANAGER
#define RIPPLE_VALIDATORS_DISABLE_MANAGER 1
#endif

/*

Information to track:

- Percentage of validations that the validator has signed
- Number of validations the validator signed that never got accepted


- Target number for Chosen
- Pseudo-randomly choose a subset from Chosen





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

--------------------------------------------------------------------------------
ChosenValidators
--------------------------------------------------------------------------------

David:
  Maybe OC should have a URL that you can query to get the latest list of URI's
  for OC-approved organzations that publish lists of validators. The server and
  client can ship with that master trust URL and also the list of URI's at the
  time it's released, in case for some reason it can't pull from OC. That would
  make the default installation safe even against major changes in the
  organizations that publish validator lists.

  The difference is that if an organization that provides lists of validators
  goes rogue, administrators don't have to act.

TODO:
  Write up from end-user perspective on the deployment and administration
  of this feature, on the wiki. "DRAFT" or "PROPOSE" to mark it as provisional.
  Template: https://ripple.com/wiki/Federation_protocol
  - What to do if you're a publisher of ValidatorList
  - What to do if you're a rippled administrator
  - Overview of how ChosenValidators works

Goals:
  Make default configuration of rippled secure.
    * Ship with TrustedUriList
    * Also have a preset RankedValidators
  Eliminate administrative burden of maintaining
  Produce the ChosenValidators list.
  Allow quantitative analysis of network health.

What determines that a validator is good?
  - Are they present (i.e. sending validations)
  - Are they on the consensus ledger
  - What percentage of consensus rounds do they participate in
  - Are they stalling consensus
    * Measurements of constructive/destructive behavior is
      calculated in units of percentage of ledgers for which
      the behavior is measured.
*/

namespace Validators
{

class ManagerImp
    : public Manager
    , private ThreadWithCallQueue::EntryPoints
    , private DeadlineTimer::Listener
    , private LeakChecked <ManagerImp>
{
public:
    explicit ManagerImp (Journal journal)
        : m_store (journal)
        , m_logic (m_store, journal)
        , m_journal (journal)
        , m_thread ("Validators")
        , m_checkTimer (this)
        , m_checkSources (true) // true to cause a full scan on start
    {
        m_thread.start (this);
    }

    ~ManagerImp ()
    {
    }

    void addStrings (String name, std::vector <std::string> const& strings)
    {
        StringArray stringArray;
        stringArray.ensureStorageAllocated (strings.size());
        for (std::size_t i = 0; i < strings.size(); ++i)
            stringArray.add (strings [i]);
        addStrings (name, stringArray);
    }

    void addStrings (String name, StringArray const& stringArray)
    {
        addStaticSource (SourceStrings::New (
            name, stringArray));
    }

    void addFile (File const& file)
    {
        addStaticSource (SourceFile::New (file));
    }

    void addURL (UniformResourceLocator const& url)
    {
        addSource (SourceURL::New (url));
    }

    //--------------------------------------------------------------------------

    void addSource (Source* source)
    {
#if RIPPLE_VALIDATORS_DISABLE_MANAGER
        delete source;
#else
        m_thread.call (&Logic::add, &m_logic, source);
#endif
    }

    void addStaticSource (Source* source)
    {
#if RIPPLE_VALIDATORS_DISABLE_MANAGER
        delete source;
#else
        m_thread.call (&Logic::addStatic, &m_logic, source);
#endif
    }

    void receiveValidation (ReceivedValidation const& rv)
    {
#if ! RIPPLE_VALIDATORS_DISABLE_MANAGER
        m_thread.call (&Logic::receiveValidation, &m_logic, rv);
#endif
    }

    //--------------------------------------------------------------------------

    void onDeadlineTimer (DeadlineTimer& timer)
    {
#if ! RIPPLE_VALIDATORS_DISABLE_MANAGER
        if (timer == m_checkTimer)
        {
            m_checkSources = true;

            // This will kick us back into threadIdle
            m_thread.interrupt();
        }
#endif
    }

    //--------------------------------------------------------------------------

    void threadInit ()
    {
#if ! RIPPLE_VALIDATORS_DISABLE_MANAGER
        File const file (File::getSpecialLocation (
            File::userDocumentsDirectory).getChildFile ("validators.sqlite"));
        
        Error error (m_store.open (file));

        if (error)
        {
            m_journal.fatal <<
                "Failed to open '" << file.getFullPathName() << "'";
        }

        if (! error)
        {
            m_logic.load ();

            // This flag needs to be on, to force a full check of all
            // sources on startup. Once we finish the check we will
            // set the deadine timer.
            //
            bassert (m_checkSources);
        }
#endif
    }

    void threadExit ()
    {
    }

    bool threadIdle ()
    {
        bool interrupted = false;

#if ! RIPPLE_VALIDATORS_DISABLE_MANAGER
        if (m_checkSources)
        {
            ThreadCancelCallback cancelCallback (m_thread);
            interrupted = m_logic.check (cancelCallback);
            if (! interrupted)
            {
                // Made it through the list without interruption!
                // Clear the flag and set the deadline timer again.
                m_checkSources = false;
                m_checkTimer.setExpiration (checkEverySeconds);
            }
        }
#endif

        return interrupted;
    }

private:
    StoreSqdb m_store;
    Logic m_logic;
    Journal m_journal;
    ThreadWithCallQueue m_thread;
    DeadlineTimer m_checkTimer;

    // True if we should call check on idle.
    // This gets set to false once we make it through the whole
    // list without interruption.
    bool m_checkSources;
};

//------------------------------------------------------------------------------

Manager* Manager::New (Journal journal)
{
    return new ManagerImp (journal);
}

}
