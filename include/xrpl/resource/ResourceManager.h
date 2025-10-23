#ifndef XRPL_RESOURCE_MANAGER_H_INCLUDED
#define XRPL_RESOURCE_MANAGER_H_INCLUDED

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/json/json_value.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Gossip.h>

#include <boost/utility/string_view.hpp>

namespace ripple {
namespace Resource {

/** Tracks load and resource consumption. */
class Manager : public beast::PropertyStream::Source
{
protected:
    Manager();

public:
    virtual ~Manager() = 0;

    /** Create a new endpoint keyed by inbound IP address or the forwarded
     * IP if proxied. */
    virtual Consumer
    newInboundEndpoint(beast::IP::Endpoint const& address) = 0;
    virtual Consumer
    newInboundEndpoint(
        beast::IP::Endpoint const& address,
        bool const proxy,
        std::string_view forwardedFor) = 0;

    /** Create a new endpoint keyed by outbound IP address and port. */
    virtual Consumer
    newOutboundEndpoint(beast::IP::Endpoint const& address) = 0;

    /** Create a new unlimited endpoint keyed by forwarded IP. */
    virtual Consumer
    newUnlimitedEndpoint(beast::IP::Endpoint const& address) = 0;

    /** Extract packaged consumer information for export. */
    virtual Gossip
    exportConsumers() = 0;

    /** Extract consumer information for reporting. */
    virtual Json::Value
    getJson() = 0;
    virtual Json::Value
    getJson(int threshold) = 0;

    /** Import packaged consumer information.
        @param origin An identifier that unique labels the origin.
    */
    virtual void
    importConsumers(std::string const& origin, Gossip const& gossip) = 0;
};

//------------------------------------------------------------------------------

std::unique_ptr<Manager>
make_Manager(
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal);

}  // namespace Resource
}  // namespace ripple

#endif
