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

/** ChosenValidators (formerly known as UNL)

    Motivation:

    To protect the integrity of the shared ledger data structure, Validators
    independently sign LedgerHash objects with their RipplePublicKey. These
    signed Validations are propagated through the peer to peer network so
    that other nodes may inspect them. Every peer and client on the network
    gains confidence in a ledger and its associated chain of previous ledgers
    by maintaining a suitably sized list of Validator public keys that it
    trusts.

    The most important factors in choosing Validators for a ChosenValidators
    list (the name we will use to designate such a list) are the following:

        - That different Validators are not controlled by one entity
        - That each Validator participates in a majority of ledgers
        - That a Validator does not sign ledgers which fail consensus

    This module maintains ChosenValidators list. The list is built from a set
    of independent Source objects, which may come from the configuration file,
    a separate file, a URL from some trusted domain, or from the network itself.

    In order that rippled administrators may publish their ChosenValidators
    list at a URL on a trusted domain that they own, this module compiles
    statistics on ledgers signed by validators and stores them in a database.
    From this database reports and alerts may be generated so that up-to-date
    information about the health of the set of ChosenValidators is always
    availabile.

    In addition to the automated statistics provided by the module, it is
    expected that organizations and meta-organizations will form from
    stakeholders such as gateways who publish their own lists and provide
    "best practices" to further refine the quality of validators placed into
    ChosenValidators list.


    ----------------------------------------------------------------------------

    Unorganized Notes:

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

namespace ripple {
namespace Validators {

class ManagerImp
    : public Manager
    , public Stoppable
    , public Thread
    , public DeadlineTimer::Listener
    , public LeakChecked <ManagerImp>
{
public:
    Journal m_journal;
    StoreSqdb m_store;
    Logic m_logic;
    DeadlineTimer m_checkTimer;
    ServiceQueue m_queue;

    // True if we should call check on idle.
    // This gets set to false once we make it through the whole list.
    //
    bool m_checkSources;

    ManagerImp (Stoppable& parent, Journal journal)
        : Stoppable ("Validators::Manager", parent)
        , Thread ("Validators")
        , m_journal (journal)
        , m_store (m_journal)
        , m_logic (m_store, m_journal)
        , m_checkTimer (this)
        , m_checkSources (false)
    {
#if BEAST_MSVC
        if (beast_isRunningUnderDebugger())
        {
            m_journal.sink().set_console (true);
            m_journal.sink().set_severity (Journal::kLowestSeverity);
        }
#endif
    }

    ~ManagerImp ()
    {
        stopThread ();
    }

    //--------------------------------------------------------------------------
    //
    // RPC::Service
    //

    Json::Value rpcPrint (Json::Value const& args)
    {
        return m_logic.rpcPrint (args);
    }

    Json::Value rpcRebuild (Json::Value const& args)
    {
        m_queue.dispatch (bind (&Logic::buildChosen, &m_logic));
        Json::Value result;
        result ["chosen_list"] = "rebuilding";
        return result;
    }

    Json::Value rpcSources (Json::Value const& args)
    {
        return m_logic.rpcSources(args);
    }

    void addRPCHandlers()
    {
        addRPCHandler ("validators_print", beast::bind (
            &ManagerImp::rpcPrint, this, beast::_1));

        addRPCHandler ("validators_rebuild", beast::bind (
            &ManagerImp::rpcRebuild, this, beast::_1));

        addRPCHandler ("validators_sources", beast::bind (
            &ManagerImp::rpcSources, this, beast::_1));
    }

    //--------------------------------------------------------------------------

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
        if (stringArray.size() > 0)
        {
            addStaticSource (SourceStrings::New (name, stringArray));
        }
        else
        {
            m_journal.debug << "Static source '" << name << "' is empty.";
        }
    }

    void addFile (File const& file)
    {
        addStaticSource (SourceFile::New (file));
    }

    void addURL (URL const& url)
    {
        addSource (SourceURL::New (url));
    }

    //--------------------------------------------------------------------------

    void addSource (Source* source)
    {
#if RIPPLE_USE_NEW_VALIDATORS
        m_queue.dispatch (bind (&Logic::add, &m_logic, source));
#else
        delete source;
#endif
    }

    void addStaticSource (Source* source)
    {
#if RIPPLE_USE_NEW_VALIDATORS
        m_queue.dispatch (bind (&Logic::addStatic, &m_logic, source));
#else
        delete source;
#endif
    }

    // VFALCO NOTE we should just do this on the callers thread?
    //
    void receiveValidation (ReceivedValidation const& rv)
    {
#if RIPPLE_USE_NEW_VALIDATORS
        if (! isStopping())
            m_queue.dispatch (bind (
                &Logic::receiveValidation, &m_logic, rv));
#endif
    }

    // VFALCO NOTE we should just do this on the callers thread?
    //
    void ledgerClosed (RippleLedgerHash const& ledgerHash)
    {
#if RIPPLE_USE_NEW_VALIDATORS
        if (! isStopping())
            m_queue.dispatch (bind (
                &Logic::ledgerClosed, &m_logic, ledgerHash));
#endif
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //

    void onPrepare ()
    {
#if RIPPLE_USE_NEW_VALIDATORS
        m_journal.info << "Validators preparing";

        addRPCHandlers();
#endif
    }

    void onStart ()
    {
#if RIPPLE_USE_NEW_VALIDATORS
        m_journal.info << "Validators starting";

        // Do this late so the sources have a chance to be added.
        m_queue.dispatch (bind (&ManagerImp::setCheckSources, this));

        startThread();
#endif
    }

    void onStop ()
    {
        m_journal.info << "Validators stopping";

        if (this->Thread::isThreadRunning())
        {
            m_journal.debug << "Signaling thread exit";
            m_queue.dispatch (bind (&Thread::signalThreadShouldExit, this));
        }
        else
        {
            stopped();
        }
    }

    //--------------------------------------------------------------------------

    void init ()
    {
        m_journal.debug << "Initializing";

        File const file (File::getSpecialLocation (
            File::userDocumentsDirectory).getChildFile ("validators.sqlite"));
        
        m_journal.debug << "Opening database at '" << file.getFullPathName() << "'";

        Error error (m_store.open (file));

        if (error)
        {
            m_journal.fatal <<
                "Failed to open '" << file.getFullPathName() << "'";
        }

        if (! error)
        {
            m_logic.load ();
        }
    }

    void onDeadlineTimer (DeadlineTimer& timer)
    {
        if (timer == m_checkTimer)
        {
            m_journal.debug << "Check timer expired";
            m_queue.dispatch (bind (&ManagerImp::setCheckSources, this));
        }
    }

    void setCheckSources ()
    {
        m_journal.debug << "Checking sources";
        m_checkSources = true;
    }

    void checkSources ()
    {
        if (m_checkSources)
        {
            if (m_logic.fetch_one () == 0)
            {
                m_journal.debug << "All sources checked";

                // Made it through the list without interruption!
                // Clear the flag and set the deadline timer again.
                //
                m_checkSources = false;

                m_journal.debug << "Next check timer expires in " <<
                    RelativeTime::seconds (checkEverySeconds);

                m_checkTimer.setExpiration (checkEverySeconds);
            }
        }
    }

    void run ()
    {
        init ();

        while (! this->threadShouldExit())
        {
            checkSources ();
            m_queue.run_one();
        }

        stopped();
    }
};

//------------------------------------------------------------------------------

Validators::Manager* Validators::Manager::New (Stoppable& parent, Journal journal)
{
    return new Validators::ManagerImp (parent, journal);
}

}
}
