#ifndef XRPL_RESOURCE_KIND_H_INCLUDED
#define XRPL_RESOURCE_KIND_H_INCLUDED

namespace ripple {
namespace Resource {

/**
 * Kind of consumer.
 * kindInbound:   Inbound connection.
 * kindOutbound:  Outbound connection.
 * kindUnlimited: Inbound connection with no resource limits, but could be
 *                subjected to administrative restrictions, such as
 *                use of some RPC commands like "stop".
 */
enum Kind { kindInbound, kindOutbound, kindUnlimited };

}  // namespace Resource
}  // namespace ripple

#endif
