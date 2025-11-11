#ifndef BEAST_INSIGHT_STATSDCOLLECTOR_H_INCLUDED
#define BEAST_INSIGHT_STATSDCOLLECTOR_H_INCLUDED

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>

namespace beast {
namespace insight {

/** A Collector that reports metrics to a StatsD server.
    Reference:
        https://github.com/b/statsd_spec
*/
class StatsDCollector : public Collector
{
public:
    explicit StatsDCollector() = default;

    /** Create a StatsD collector.
        @param address The IP address and port of the StatsD server.
        @param prefix A string pre-pended before each metric name.
        @param journal Destination for logging output.
    */
    static std::shared_ptr<StatsDCollector>
    New(IP::Endpoint const& address,
        std::string const& prefix,
        Journal journal);
};

}  // namespace insight
}  // namespace beast

#endif
