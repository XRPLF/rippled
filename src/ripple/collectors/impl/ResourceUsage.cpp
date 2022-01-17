#include <ripple/basics/Log.h>
#include <ripple/collectors/ResourceUsage.h>
#include <ripple/collectors/impl/ResourceUsageImpl.h>
#include <exception>

namespace ripple {
namespace collectors {

ResourceUsage::ResourceUsage(beast::Journal journal) : m_pimpl()
{
    try
    {
        m_pimpl = std::make_unique<ResourceUsageImpl>(journal);
    }
    catch (const std::exception& exc)
    {
        JLOG(journal.error())
            << "Error during ResourceUsageImpl::ResourceUsageImpl(). We will "
               "not be able to collect resource usage statistics. Exc: "
            << exc.what();
    }
    catch (...)
    {
        JLOG(journal.error())
            << "Unknown error during ResourceUsageImpl::ResourceUsageImpl(). "
               "We will not be able to collect resource usage statistics.";
    }
}

ResourceUsage::~ResourceUsage() = default;

ResultMap
ResourceUsage::getResourceUsage()
{
    static const ResultMap empty{};
    if (nullptr == m_pimpl)
    {
        return empty;
    }

    return m_pimpl->getResourceUsage();
}

}  // namespace collectors
}  // namespace ripple
