#pragma once

namespace xrpl::Resource {

/**
 * Kind of consumer.
 * kindInbound:   Inbound connection.
 * kindOutbound:  Outbound connection.
 * kindUnlimited: Inbound connection with no resource limits, but could be
 *                subjected to administrative restrictions, such as
 *                use of some RPC commands like "stop".
 */
enum class Kind { kindInbound, kindOutbound, kindUnlimited };

}  // namespace xrpl::Resource
