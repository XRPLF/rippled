#pragma once

#include <ripple/beast/utility/Journal.h>
#include <ripple/collectors/impl/ResultMap.h>
#include <memory>

namespace ripple {
namespace collectors {

class ResourceUsageImpl;

/**
 * @brief Retrieves and calculates system and process resource usage statistics.
 */
class ResourceUsage
{
public:
    ResourceUsage(beast::Journal journal);
    ~ResourceUsage();

    ResourceUsage(const ResourceUsage&) = delete;
    ResourceUsage& operator=(const ResourceUsage&) = delete;

    /**
     * @brief Get the ResultMap, containing system and process resource usage.
     * @return ResultMap Contains resource usage key/value pairs.
     */
    ResultMap getResourceUsage();

private:
    /// Contains the implementation and required dependencies.
    std::unique_ptr<ResourceUsageImpl> m_pimpl;
};

}  // namespace collectors
}  // namespace ripple
