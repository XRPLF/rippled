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

#include <BeastConfig.h>
#include <ripple/validators/ValidatorManager.h>
#include <ripple/validators/make_Manager.h>
#include <ripple/validators/impl/ConnectionImp.h>
#include <ripple/validators/impl/Logic.h>
#include <ripple/validators/impl/StoreSqdb.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/waitable_executor.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <memory>

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

#include <ripple/core/JobQueue.h>
#include <memory>

namespace ripple {

/** Executor which dispatches to JobQueue threads at a given JobType. */
class job_executor
{
private:
    struct impl
    {
        impl (JobQueue& ex_, JobType type_, std::string const& name_)
            : ex(ex_), type(type_), name(name_)
        {
        }

        JobQueue& ex;
        JobType type;
        std::string name;
    };

    std::shared_ptr<impl> impl_;

public:
    job_executor (JobType type, std::string const& name,
            JobQueue& ex)
        : impl_(std::make_shared<impl>(ex, type, name))
    {
    }

    template <class Handler>
    void
    post (Handler&& handler)
    {
        impl_->ex.addJob(impl_->type, impl_->name,
            std::forward<Handler>(handler));
    }

    template <class Handler>
    void
    dispatch (Handler&& handler)
    {
        impl_->ex.addJob(impl_->type, impl_->name,
            std::forward<Handler>(handler));
    }

    template <class Handler>
    void
    defer (Handler&& handler)
    {
        impl_->ex.addJob(impl_->type, impl_->name,
            std::forward<Handler>(handler));
    }
};

//------------------------------------------------------------------------------

namespace Validators {

// template <class Executor>
class ManagerImp
    : public Manager
    , public beast::Stoppable
{
public:
    boost::asio::io_service& io_service_;
    boost::asio::io_service::strand strand_;
    beast::asio::waitable_executor exec_;
    boost::asio::basic_waitable_timer<
        std::chrono::steady_clock> timer_;
    beast::Journal journal_;
    StoreSqdb store_;
    Logic logic_;
    SociConfig sociConfig_;

    ManagerImp (Stoppable& parent, boost::asio::io_service& io_service,
        beast::Journal journal, BasicConfig const& config)
        : Stoppable ("Validators::Manager", parent)
        , io_service_(io_service)
        , strand_(io_service_)
        , timer_(io_service_)
        , journal_ (journal)
        , store_ (journal_)
        , logic_ (store_, journal_)
        , sociConfig_ (config, "validators")
    {
    }

    ~ManagerImp()
    {
    }

    //--------------------------------------------------------------------------
    //
    // Manager
    //
    //--------------------------------------------------------------------------

    std::unique_ptr<Connection>
    newConnection (int id) override
    {
        return std::make_unique<ConnectionImp>(
            id, logic_, stopwatch());
    }

    void
    onLedgerClosed (LedgerIndex index,
        LedgerHash const& hash, LedgerHash const& parent) override
    {
        logic_.onLedgerClosed (index, hash, parent);
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare()
    {
        init();
    }

    void onStart()
    {
    }

    void onStop()
    {
        boost::system::error_code ec;
        timer_.cancel(ec);

        logic_.stop();

        exec_.async_wait([this]() { stopped(); });
    }

    //--------------------------------------------------------------------------
    //
    // PropertyStream
    //
    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map)
    {
    }

    //--------------------------------------------------------------------------
    //
    // ManagerImp
    //
    //--------------------------------------------------------------------------

    void init()
    {
        store_.open (sociConfig_);
        logic_.load ();
    }

    void
    onTimer (boost::system::error_code ec)
    {
        if (ec)
        {
            if (ec != boost::asio::error::operation_aborted)
                journal_.error <<
                    "onTimer: " << ec.message();
            return;
        }

        logic_.onTimer();

        timer_.expires_from_now(std::chrono::seconds(1), ec);
        timer_.async_wait(strand_.wrap(exec_.wrap(
            std::bind(&ManagerImp::onTimer, this,
                beast::asio::placeholders::error))));
    }
};

//------------------------------------------------------------------------------

Manager::Manager ()
    : beast::PropertyStream::Source ("validators")
{
}

std::unique_ptr<Manager>
make_Manager(beast::Stoppable& parent,
    boost::asio::io_service& io_service,
    beast::Journal journal,
    BasicConfig const& config)
{
    return std::make_unique<ManagerImp>(parent,
            io_service, journal, config);
}
}
}
