#include <ripple/collectors/ResourceUsageCollectorBase.h>
#include <boost/predef.h>

#include <ripple/collectors/impl/default/ResourceUsageCollectorDefault.h>
#if (RIPPLED_RESOURCE_REPORT && BOOST_OS_LINUX)
#include <ripple/collectors/impl/linux/ResourceUsageCollectorLinux.h>
#endif

#include <exception>

using namespace ripple::collectors;

// static
std::shared_ptr<ResourceUsageCollectorBase> ResourceUsageCollectorBase::create(
    beast::Journal journal,
    boost::asio::io_service& io_service)
{
    try
    {
#if (RIPPLED_RESOURCE_REPORT && BOOST_OS_LINUX)
        auto collector = std::make_shared<ResourceUsageCollectorLinux>(journal, io_service);
        collector->setTimer();
        return collector;
#else // Add MacOS, Windows instance creation here
#endif
    }
    catch (const std::exception& exc)
    {
        JLOG(journal.error())
            << "Error during ResourceUsageCollectorBase::create(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(journal.error())
            << "Unknown error during ResourceUsageCollectorBase::create()";
    }

    return std::make_shared<ResourceUsageCollectorDefault>(journal, io_service);
}

ResourceUsageCollectorBase::ResourceUsageCollectorBase(
    beast::Journal journal,
    boost::asio::io_service& io_service)
    : journal_(journal)
    , strand_(io_service)
    , timer_(waitable_timer(io_service))
    , mutex_()
    , resultMap_()
{
}

ResourceUsageCollectorBase::~ResourceUsageCollectorBase()
{
    this->cancelTimer();
}

ResultMap
ResourceUsageCollectorBase::resultMap() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->resultMap_;
}

void
ResourceUsageCollectorBase::setResultMap(ResultMap const& resultMap)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->resultMap_ = resultMap;
}

void
ResourceUsageCollectorBase::setTimer()
{
    error_code ec;
    timer_.expires_from_now(resourceCollectionTimerInterval, ec);
    if (ec)
    {
        JLOG(this->journal().error()) << "setTimer: " << ec.message();
        return;
    }

    timer_.async_wait(strand_.wrap(std::bind(
        &ResourceUsageCollectorBase::onTimer, shared_from_this(), std::placeholders::_1)));
}

void
ResourceUsageCollectorBase::cancelTimer()
{
    error_code ec;
    timer_.cancel(ec);

    if (ec)
    {
        JLOG(this->journal().error()) << "cancelTimer: " << ec.message();
        return;
    }
}

void
ResourceUsageCollectorBase::onTimer(error_code const& ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    try
    {
        this->collect();
    }
    catch (const std::exception& exc)
    {
        JLOG(this->journal().error())
            << "Error during ResourceUsageCollectorBase::collect(): "
            << exc.what();
    }
    catch (...)
    {
        JLOG(this->journal().error())
            << "Unknown error during ResourceUsageCollectorBase::collect()";
    }

    setTimer();
}
