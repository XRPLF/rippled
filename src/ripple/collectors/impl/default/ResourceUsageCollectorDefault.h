#pragma once

#include <ripple/collectors/ResourceUsageCollectorBase.h>

namespace ripple {
namespace collectors {

/**
 * @brief Dummy/faalback resource usage collector, ResultMap will be empty.
 */
class ResourceUsageCollectorDefault : public ResourceUsageCollectorBase
{
public:
    explicit ResourceUsageCollectorDefault(
        beast::Journal journal,
        boost::asio::io_service& io_service);
    ~ResourceUsageCollectorDefault() override;

    ResourceUsageCollectorDefault(const ResourceUsageCollectorDefault&) = delete;
    ResourceUsageCollectorDefault& operator=(const ResourceUsageCollectorDefault&) = delete;

    void collect() override;
};

}  // namespace collectors
}  // namespace ripple
