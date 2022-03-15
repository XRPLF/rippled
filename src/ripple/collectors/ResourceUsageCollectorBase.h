#pragma once

#include <ripple/basics/Log.h>
#include <ripple/beast/utility/Journal.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ripple {
namespace collectors {

/**
 * @brief The map, containing the resource usage statistics.
 */
using ResultMap = std::map<std::string, float>;

/**
 * @brief Responsible for periodic collection of resource usage metrics.
 */
class ResourceUsageCollectorBase : public std::enable_shared_from_this<ResourceUsageCollectorBase>
{
public:
    /**
     * @brief Creates the platform specific instance for resource usage metrics collection.
     * @param journal Used for logging.
     * @param io_service Used to execute the collection timer.
     * @return shared_ptr to the created instance.
     */
    static std::shared_ptr<ResourceUsageCollectorBase> create(
        beast::Journal journal,
        boost::asio::io_service& io_service);

    virtual ~ResourceUsageCollectorBase();

    /**
     * @brief Get the ResultMap, containing system and process resource usage.
     * @return ResultMap Contains resource usage key/value pairs.
     */
    ResultMap resultMap() const;

protected:
    /**
     * @brief Construct a new instance of ResourceUsageCollectorBase
     * @param journal Used for logging.
     * @param io_service Used to execute the collection timer.
     */
    explicit ResourceUsageCollectorBase(
        beast::Journal journal,
        boost::asio::io_service& io_service);

    /**
     * @brief Collects the resource usage metrics.
     * @note Called by the collection timer.
     */
    virtual void
    collect() = 0;

    beast::Journal&
    journal() { return journal_; };

    /**
     * @brief Sets the ResultMap.
     * @param resultMap The ResultMap to set.
     */
    void
    setResultMap(ResultMap const& resultMap);

private:
    using error_code = boost::system::error_code;
    using waitable_timer =
        boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

    /// The interval to apply by the collection timer.
    static std::chrono::seconds constexpr resourceCollectionTimerInterval{60};

    /**
     * @brief Starts the collection timer.
     */
    void
    setTimer();

    /**
     * @brief Cancels the collection timer.
     */
    void
    cancelTimer();

    /**
     * @brief The collection timer callback, executes the collect() method.
     * @param ec The error code.
     */
    void
    onTimer(error_code const& ec);

    /// Used for logging.
    beast::Journal journal_;
    /// Used to execute the timer callback.
    boost::asio::io_service::strand strand_;
    /// The collection timer.
    waitable_timer timer_;

    /// Protects the resultMap member.
    mutable std::mutex mutex_;
    /// Contains the most recently collected resource usage metrics.
    ResultMap resultMap_;
};

}  // namespace collectors
}  // namespace ripple
