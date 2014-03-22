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
          
    What we want from the unique node list:
      - Some number of trusted roots (known by domain)
        probably organizations whose job is to provide a list of validators
      - We imagine the IRGA for example would establish some group whose job is to
        maintain a list of validators. There would be a public list of criteria
        that they would use to vet the validator. Things like:
        * Not anonymous
        * registered business
        * Physical location
        * Agree not to cease operations without notice / arbitrarily
        * Responsive to complaints
      - Identifiable jurisdiction
        * Homogeneity in the jurisdiction is a business risk
        * If all validators are in the same jurisdiction this is a business risk
      - OpenCoin sets criteria for the organizations
      - Rippled will ship with a list of trusted root "certificates"
        In other words this is a list of trusted domains from which the software
          can contact each trusted root and retrieve a list of "good" validators
          and then do something with that information
      - All the validation information would be public, including the broadcast
        messages.
      - The goal is to easily identify bad actors and assess network health
        * Malicious intent
        * Or, just hardware problems (faulty drive or memory)


*/

namespace ripple {
namespace Validators {

class ManagerImp
    : public Manager
    , public beast::Stoppable
    , public beast::Thread
    , public beast::DeadlineTimer::Listener
    , public beast::LeakChecked <ManagerImp>
{
public:
    beast::Journal m_journal;
    beast::File m_databaseFile;
    StoreSqdb m_store;
    Logic m_logic;
    beast::DeadlineTimer m_checkTimer;
    beast::ServiceQueue m_queue;

    typedef beast::ScopedWrapperContext <
        beast::RecursiveMutex, beast::RecursiveMutex::ScopedLockType> Context;

    Context m_context;

    // True if we should call check on idle.
    // This gets set to false once we make it through the whole list.
    //
    bool m_checkSources;

    ManagerImp (
        Stoppable& parent, 
        beast::File const& pathToDbFileOrDirectory, 
        beast::Journal journal)
        : Stoppable ("Validators::Manager", parent)
        , Thread ("Validators")
        , m_journal (journal)
        , m_databaseFile (pathToDbFileOrDirectory)
        , m_store (m_journal)
        , m_logic (m_store, m_journal)
        , m_checkTimer (this)
        , m_checkSources (false)
    {
        m_journal.trace <<
            "Validators constructed";
        m_journal.debug <<
            "Validators constructed (debug)";
        m_journal.info <<
            "Validators constructed (info)";

        if (m_databaseFile.isDirectory ())
            m_databaseFile = m_databaseFile.getChildFile("validators.sqlite");


    }

    ~ManagerImp ()
    {
        stopThread ();
    }

    //--------------------------------------------------------------------------
    //
    // Manager
    //
    //--------------------------------------------------------------------------

    void addStrings (beast::String name, std::vector <std::string> const& strings)
    {
        beast::StringArray stringArray;
        stringArray.ensureStorageAllocated (strings.size());
        for (std::size_t i = 0; i < strings.size(); ++i)
            stringArray.add (strings [i]);
        addStrings (name, stringArray);
    }

    void addStrings (beast::String name, beast::StringArray const& stringArray)
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

    void addFile (beast::File const& file)
    {
        addStaticSource (SourceFile::New (file));
    }

    void addStaticSource (Validators::Source* source)
    {
        m_queue.dispatch (m_context.wrap (std::bind (
            &Logic::addStatic, &m_logic, source)));
    }

    void addURL (beast::URL const& url)
    {
        addSource (SourceURL::New (url));
    }

    void addSource (Validators::Source* source)
    {
        m_queue.dispatch (m_context.wrap (std::bind (
            &Logic::add, &m_logic, source)));
    }

    //--------------------------------------------------------------------------

    void receiveValidation (ReceivedValidation const& rv)
    {
        if (! isStopping())
            m_queue.dispatch (m_context.wrap (std::bind (
                &Logic::receiveValidation, &m_logic, rv)));
    }

    void ledgerClosed (RippleLedgerHash const& ledgerHash)
    {
        if (! isStopping())
            m_queue.dispatch (m_context.wrap (std::bind (
                &Logic::ledgerClosed, &m_logic, ledgerHash)));
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare ()
    {
    }

    void onStart ()
    {
        // Do this late so the sources have a chance to be added.
        m_queue.dispatch (m_context.wrap (std::bind (
            &ManagerImp::setCheckSources, this)));

        startThread();
    }

    void onStop ()
    {
        m_logic.stop ();

        m_queue.dispatch (m_context.wrap (std::bind (
            &Thread::signalThreadShouldExit, this)));
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map)
    {
        Context::Scope scope (m_context);

        map ["trusted"] = std::uint32_t (
            m_logic.m_chosenList ?
                m_logic.m_chosenList->size() : 0);

        {
            beast::PropertyStream::Set items ("sources", map);
            for (Logic::SourceTable::const_iterator iter (m_logic.m_sources.begin());
                iter != m_logic.m_sources.end(); ++iter)
                items.add (iter->source->to_string());
        }

        {
            beast::PropertyStream::Set items ("validators", map);
            for (Logic::ValidatorTable::iterator iter (m_logic.m_validators.begin());
                iter != m_logic.m_validators.end(); ++iter)
            {
                RipplePublicKey const& publicKey (iter->first);
                Validator const& validator (iter->second);
                beast::PropertyStream::Map item (items);
                item["public_key"] = publicKey.to_string();
                validator.count().onWrite (item);
            }
        }
    }

    //--------------------------------------------------------------------------
    //
    // ManagerImp
    //
    //--------------------------------------------------------------------------

    void init ()
    {
        beast::Error error (m_store.open (m_databaseFile));
        
        if (! error)
        {
            m_logic.load ();
        }
    }

    void onDeadlineTimer (beast::DeadlineTimer& timer)
    {
        if (timer == m_checkTimer)
        {
            m_journal.trace << "Check timer expired";
            m_queue.dispatch (m_context.wrap (std::bind (
                &ManagerImp::setCheckSources, this)));
        }
    }

    void setCheckSources ()
    {
        m_journal.trace << "Checking sources";
        m_checkSources = true;
    }

    void checkSources ()
    {
        if (m_checkSources)
        {
            if (m_logic.fetch_one () == 0)
            {
                m_journal.trace << "All sources checked";

                // Made it through the list without interruption!
                // Clear the flag and set the deadline timer again.
                //
                m_checkSources = false;

                m_journal.trace << "Next check timer expires in " <<
                    beast::RelativeTime::seconds (checkEverySeconds);

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

Manager::Manager ()
    : beast::PropertyStream::Source ("validators")
{
}

Validators::Manager* Validators::Manager::New (
    beast::Stoppable& parent, 
    beast::File const& pathToDbFileOrDirectory,
    beast::Journal journal)
{
    return new Validators::ManagerImp (parent, pathToDbFileOrDirectory, journal);
}

}
}
