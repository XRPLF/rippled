#include <xrpld/app/main/CollectorManager.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Group.h>
#include <xrpl/beast/insight/Groups.h>
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/insight/StatsDCollector.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>

#include <memory>
#include <string>

namespace xrpl {

class CollectorManagerImp : public CollectorManager
{
public:
    beast::Journal m_journal;
    beast::insight::Collector::ptr m_collector;
    std::unique_ptr<beast::insight::Groups> m_groups;

    CollectorManagerImp(Section const& params, beast::Journal journal) : m_journal(journal)
    {
        std::string const& server = get(params, "server");

        if (server == "statsd")
        {
            beast::IP::Endpoint const address(
                beast::IP::Endpoint::from_string(get(params, "address")));
            std::string const& prefix(get(params, "prefix"));

            m_collector = beast::insight::StatsDCollector::New(address, prefix, journal);
        }
        else
        {
            m_collector = beast::insight::NullCollector::New();
        }

        m_groups = beast::insight::make_Groups(m_collector);
    }

    ~CollectorManagerImp() override = default;

    beast::insight::Collector::ptr const&
    collector() override
    {
        return m_collector;
    }

    beast::insight::Group::ptr const&
    group(std::string const& name) override
    {
        return m_groups->get(name);
    }
};

//------------------------------------------------------------------------------

std::unique_ptr<CollectorManager>
make_CollectorManager(Section const& params, beast::Journal journal)
{
    return std::make_unique<CollectorManagerImp>(params, journal);
}

}  // namespace xrpl
