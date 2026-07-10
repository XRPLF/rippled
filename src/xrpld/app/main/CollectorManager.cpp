#include <xrpld/app/main/CollectorManager.h>

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Group.h>
#include <xrpl/beast/insight/Groups.h>
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/insight/OTelCollector.h>
#include <xrpl/beast/insight/StatsDCollector.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>

#include <memory>
#include <string>

namespace xrpl {

class CollectorManagerImp : public CollectorManager
{
public:
    // NOLINTBEGIN(readability-identifier-naming)
    beast::Journal journal_;
    beast::insight::Collector::ptr collector_;
    std::unique_ptr<beast::insight::Groups> groups_;
    // NOLINTEND(readability-identifier-naming)

    CollectorManagerImp(
        Section const& params,
        std::string const& serviceName,
        std::string const& networkType,
        beast::Journal journal)
        : journal_(journal)
    {
        std::string const& server = get(params, Keys::kServer);

        if (server == "statsd")
        {
            beast::IP::Endpoint const address(
                beast::IP::Endpoint::fromString(get(params, Keys::kAddress)));
            std::string const& prefix(get(params, Keys::kPrefix));

            collector_ = beast::insight::StatsDCollector::make(address, prefix, journal);
        }
        // LCOV_EXCL_START -- OTel collector path is not exercised in unit tests
        else if (server == "otel")
        {
            // Read OTLP metrics endpoint from [insight] section.
            // Default to the standard OTLP/HTTP metrics path on localhost.
            std::string endpoint = get(params, "endpoint");
            if (endpoint.empty())
                endpoint = "http://localhost:4318/v1/metrics";
            std::string const& prefix(get(params, "prefix"));

            // Read service_instance_id, same key as the [telemetry]
            // section uses, so multi-node deployments can distinguish
            // metric sources via the service_instance_id Prometheus label.
            std::string const instanceId = get(params, "service_instance_id");

            // service.name from [insight] (falls back to the value the
            // caller derived from [telemetry]); network type derived from
            // [network_id]. Both mirror the trace exporter so metrics and
            // traces carry the same service and network identity.
            std::string serviceNameCfg = get(params, "service_name");
            if (serviceNameCfg.empty())
                serviceNameCfg = serviceName;

            collector_ = beast::insight::OTelCollector::New(
                endpoint, prefix, instanceId, serviceNameCfg, networkType, journal);
        }
        // LCOV_EXCL_STOP
        else
        {
            collector_ = beast::insight::NullCollector::make();
        }

        groups_ = beast::insight::makeGroups(collector_);
    }

    ~CollectorManagerImp() override = default;

    beast::insight::Collector::ptr const&
    collector() override
    {
        return collector_;
    }

    beast::insight::Group::ptr const&
    group(std::string const& name) override
    {
        return groups_->get(name);
    }
};

//------------------------------------------------------------------------------

std::unique_ptr<CollectorManager>
makeCollectorManager(
    Section const& params,
    std::string const& serviceName,
    std::string const& networkType,
    beast::Journal journal)
{
    return std::make_unique<CollectorManagerImp>(params, serviceName, networkType, journal);
}

}  // namespace xrpl
