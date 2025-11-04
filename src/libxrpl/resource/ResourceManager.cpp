#include <xrpl/basics/chrono.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/net/IPAddressConversion.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/json/json_value.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Gossip.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/resource/detail/Logic.h>

#include <boost/asio/ip/address.hpp>
#include <boost/system/detail/error_code.hpp>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace ripple {
namespace Resource {

class ManagerImp : public Manager
{
private:
    beast::Journal const journal_;
    Logic logic_;
    std::thread thread_;
    bool stop_ = false;
    std::mutex mutex_;
    std::condition_variable cond_;

public:
    ManagerImp(
        beast::insight::Collector::ptr const& collector,
        beast::Journal journal)
        : journal_(journal), logic_(collector, stopwatch(), journal)
    {
        thread_ = std::thread{&ManagerImp::run, this};
    }

    ManagerImp() = delete;
    ManagerImp(ManagerImp const&) = delete;
    ManagerImp&
    operator=(ManagerImp const&) = delete;

    ~ManagerImp() override
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
            cond_.notify_one();
        }
        thread_.join();
    }

    Consumer
    newInboundEndpoint(beast::IP::Endpoint const& address) override
    {
        return logic_.newInboundEndpoint(address);
    }

    Consumer
    newInboundEndpoint(
        beast::IP::Endpoint const& address,
        bool const proxy,
        std::string_view forwardedFor) override
    {
        if (!proxy)
            return newInboundEndpoint(address);

        boost::system::error_code ec;
        auto const proxiedIp = boost::asio::ip::make_address(forwardedFor, ec);
        if (ec)
        {
            journal_.warn()
                << "forwarded for (" << forwardedFor << ") from proxy "
                << address.to_string()
                << " doesn't convert to IP endpoint: " << ec.message();
            return newInboundEndpoint(address);
        }
        return newInboundEndpoint(
            beast::IPAddressConversion::from_asio(proxiedIp));
    }

    Consumer
    newOutboundEndpoint(beast::IP::Endpoint const& address) override
    {
        return logic_.newOutboundEndpoint(address);
    }

    Consumer
    newUnlimitedEndpoint(beast::IP::Endpoint const& address) override
    {
        return logic_.newUnlimitedEndpoint(address);
    }

    Gossip
    exportConsumers() override
    {
        return logic_.exportConsumers();
    }

    void
    importConsumers(std::string const& origin, Gossip const& gossip) override
    {
        logic_.importConsumers(origin, gossip);
    }

    //--------------------------------------------------------------------------

    Json::Value
    getJson() override
    {
        return logic_.getJson();
    }

    Json::Value
    getJson(int threshold) override
    {
        return logic_.getJson(threshold);
    }

    //--------------------------------------------------------------------------

    void
    onWrite(beast::PropertyStream::Map& map) override
    {
        logic_.onWrite(map);
    }

    //--------------------------------------------------------------------------

private:
    void
    run()
    {
        beast::setCurrentThreadName("Resource::Manager");
        for (;;)
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

Manager::Manager() : beast::PropertyStream::Source("resource")
{
}

Manager::~Manager() = default;

//------------------------------------------------------------------------------

std::unique_ptr<Manager>
make_Manager(
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal)
{
    return std::make_unique<ManagerImp>(collector, journal);
}

}  // namespace Resource
}  // namespace ripple
