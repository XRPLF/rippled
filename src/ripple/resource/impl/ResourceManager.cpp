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

#include <ripple/resource/ResourceManager.h>
#include <ripple/resource/impl/Logic.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace ripple {
namespace Resource {

class ManagerImp : public Manager
{
private:
    beast::Journal journal_;
    Logic logic_;
    std::thread thread_;
    bool stop_ = false;
    std::mutex mutex_;
    std::condition_variable cond_;

public:
    ManagerImp (beast::insight::Collector::ptr const& collector,
        beast::Journal journal)
        : journal_ (journal)
        , logic_ (collector, stopwatch(), journal)
    {
        thread_ = std::thread {&ManagerImp::run, this};
    }

    ManagerImp () = delete;
    ManagerImp (ManagerImp const&) = delete;
    ManagerImp& operator= (ManagerImp const&) = delete;

    ~ManagerImp () override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            cond_.notify_one();
        }
        thread_.join();
    }

    Consumer newInboundEndpoint (beast::IP::Endpoint const& address) override
    {
        return logic_.newInboundEndpoint (address);
    }

    Consumer newOutboundEndpoint (beast::IP::Endpoint const& address) override
    {
        return logic_.newOutboundEndpoint (address);
    }

    Consumer newUnlimitedEndpoint (std::string const& name) override
    {
        return logic_.newUnlimitedEndpoint (name);
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
        beast::setCurrentThreadName ("Resource::Manager");
        for(;;)
        {
            logic_.periodicActivity();
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait_for(lock, std::chrono::seconds(1));
            if (stop_)
                break;
        }
    }
};

//------------------------------------------------------------------------------

Manager::Manager ()
    : beast::PropertyStream::Source ("resource")
{
}

Manager::~Manager() = default;

//------------------------------------------------------------------------------

std::unique_ptr <Manager> make_Manager (
    beast::insight::Collector::ptr const& collector,
        beast::Journal journal)
{
    return std::make_unique <ManagerImp> (collector, journal);
}

}
}
