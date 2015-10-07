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
#include <ripple/resource/ResourceManager.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>  // JLOG
#include <beast/threads/Thread.h>
#include <memory>

namespace ripple {
namespace Resource {

class ManagerImp : public Manager
{
private:
    beast::Journal journal_;
    Logic logic_;
    std::thread thread_;
    std::atomic<bool> run_;

public:
    ManagerImp (beast::insight::Collector::ptr const& collector,
        beast::Journal journal)
        : journal_ (journal)
        , logic_ (collector, stopwatch(), journal)
        , thread_ ()
        , run_ (true)
    {
        thread_ = std::thread {&ManagerImp::run, this};
    }

    ManagerImp () = delete;
    ManagerImp (ManagerImp const&) = delete;
    ManagerImp& operator= (ManagerImp const&) = delete;

    ~ManagerImp () override
    {
        run_.store (false);
        try
        {
            thread_.join();
        }
        catch (std::exception ex)
        {
            // Swallow the exception in a destructor.
            JLOG(journal_.warning) << "std::exception in Resource::~Manager.  "
                << ex.what();
        }
    }

    Consumer newInboundEndpoint (beast::IP::Endpoint const& address) override
    {
        return logic_.newInboundEndpoint (address);
    }

    Consumer newOutboundEndpoint (beast::IP::Endpoint const& address) override
    {
        return logic_.newOutboundEndpoint (address);
    }

    Consumer newAdminEndpoint (std::string const& name) override
    {
        return logic_.newAdminEndpoint (name);
    }

    Gossip exportConsumers () override
    {
        return logic_.exportConsumers();
    }

    void importConsumers (
        std::string const& origin, Gossip const& gossip) override
    {
        logic_.importConsumers (origin, gossip);
    }

    //--------------------------------------------------------------------------

    Json::Value getJson () override
    {
        return logic_.getJson ();
    }

    Json::Value getJson (int threshold) override
    {
        return logic_.getJson (threshold);
    }

    //--------------------------------------------------------------------------

    void onWrite (beast::PropertyStream::Map& map) override
    {
        logic_.onWrite (map);
    }

    //--------------------------------------------------------------------------

private:
    void run ()
    {
        beast::Thread::setCurrentThreadName ("Resource::Manager");
        do
        {
            logic_.periodicActivity();
            std::this_thread::sleep_for (std::chrono::seconds (1));
        }
        while (run_.load());
    }
};

//------------------------------------------------------------------------------

Manager::Manager ()
    : beast::PropertyStream::Source ("resource")
{
}

Manager::~Manager ()
{
}

//------------------------------------------------------------------------------

std::unique_ptr <Manager> make_Manager (
    beast::insight::Collector::ptr const& collector,
        beast::Journal journal)
{
    return std::make_unique <ManagerImp> (collector, journal);
}

}
}
