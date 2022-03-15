#include <ripple/collectors/impl/default/ResourceUsageCollectorDefault.h>

using namespace ripple::collectors;

ResourceUsageCollectorDefault::ResourceUsageCollectorDefault(
    beast::Journal journal,
    boost::asio::io_service& io_service)
    : ResourceUsageCollectorBase(journal, io_service)
{
    JLOG(this->journal().info())
        << "Constructed dummy ResourceUsageCollector.";
}

ResourceUsageCollectorDefault::~ResourceUsageCollectorDefault() = default;

void
ResourceUsageCollectorDefault::collect()
{
}
