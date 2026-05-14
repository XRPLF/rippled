#pragma once

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/server/Port.h>

#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>
#include <boost/utility/string_view.hpp>

#include <string>
#include <vector>

namespace xrpl {

/** Privilege level assigned to an inbound RPC or WebSocket connection.
 *
 *  Every request is classified into one of these roles by `requestRole()`.
 *  Downstream code gates commands and resource consumption on the result.
 *
 *  The separation of `IDENTIFIED` from `ADMIN` is intentional: a trusted
 *  reverse proxy (secure_gateway) can carry high-throughput user traffic
 *  with per-user rate exemption without exposing admin-only commands such
 *  as `stop` or `ledger_request` to those end users.
 */
enum class Role {
    GUEST,       /**< Unauthenticated external caller; subject to rate limiting. */
    USER,        /**< Reserved; not currently assigned by `requestRole()`. */
    IDENTIFIED,  /**< Caller identified via `X-User` header through a trusted
                      secure_gateway proxy; unlimited resources but cannot run
                      all admin-only commands. */
    ADMIN,       /**< Caller from an admin-listed IP with correct credentials;
                      full command access and unlimited resources. */
    PROXY,       /**< Trusted secure_gateway connection without a forwarded user
                      identity; unlimited for forwarding, but individual end
                      users are rate-limited by their forwarded IP. */
    FORBID,      /**< Access denied; returned when `ADMIN` was required but the
                      IP/credential check failed. */
};

/** Determine the privilege role for an inbound request.
 *
 *  Evaluates three checks in order:
 *  1. Admin: remote IP in `port.admin_nets_*` AND credentials correct →
 *     returns `Role::ADMIN` immediately.
 *  2. Required-but-denied: if `required == Role::ADMIN` and step 1 failed →
 *     returns `Role::FORBID`.
 *  3. Secure gateway: remote IP in `port.secure_gateway_nets_*` → returns
 *     `Role::IDENTIFIED` when `user` is non-empty, `Role::PROXY` otherwise.
 *  4. Fallthrough → `Role::GUEST`.
 *
 *  @param required   Minimum role the caller must have; pass `Role::ADMIN`
 *      to reject non-admin callers with `Role::FORBID` rather than
 *      `Role::GUEST`.
 *  @param port       Port configuration carrying `admin_nets_*`,
 *      `secure_gateway_nets_*`, `admin_user`, and `admin_password`.
 *  @param params     JSON-RPC request parameters; the optional fields
 *      `admin_user` and `admin_password` are read for credential validation.
 *  @param remoteIp   The physical remote endpoint of the connection.
 *  @param user       Value of the `X-User` HTTP header forwarded by a
 *      secure_gateway proxy; empty string if not present. Untrusted callers
 *      cannot self-elevate by sending this header — the IP gate runs first.
 *  @return The assigned `Role`.
 */
Role
requestRole(
    Role const& required,
    Port const& port,
    json::Value const& params,
    beast::IP::Endpoint const& remoteIp,
    std::string_view user);

/** Obtain a `Resource::Consumer` appropriate for the assigned role.
 *
 *  Translates a role into the correct resource-manager endpoint type:
 *  - `ADMIN` or `IDENTIFIED` (`isUnlimited`) → unlimited endpoint; bypasses
 *    all throttling.
 *  - `PROXY` → proxied inbound endpoint keyed on `forwardedFor`, so each
 *    end-user behind the proxy gets an independent rate-limit bucket.
 *  - `GUEST` → standard rate-limited endpoint keyed on `remoteAddress`.
 *
 *  @param manager        Resource manager that owns the consumer lifetime.
 *  @param remoteAddress  Physical remote endpoint used as the consumer key
 *      for non-proxied connections and for unlimited endpoints.
 *  @param role           Role previously assigned by `requestRole()`.
 *  @param user           Forwarded user identity (used by the resource
 *      manager for logging; may be empty).
 *  @param forwardedFor   Originating client IP extracted from
 *      `X-Forwarded-For` or `Forwarded`; used as the consumer key when
 *      `role == Role::PROXY`.
 *  @return A `Resource::Consumer` tracking this connection's resource usage.
 */
Resource::Consumer
requestInboundEndpoint(
    Resource::Manager& manager,
    beast::IP::Endpoint const& remoteAddress,
    Role const& role,
    std::string_view user,
    std::string_view forwardedFor);

/** Returns true if the role is entitled to unlimited resources.
 *
 *  Only `ADMIN` and `IDENTIFIED` are unlimited. All other roles, including
 *  `PROXY`, are subject to normal rate limiting.
 *
 *  @param role The role to test.
 *  @return `true` if `role` is `ADMIN` or `IDENTIFIED`.
 */
bool
isUnlimited(Role const& role);

/** Returns true if `remoteIp` falls within any of the given CIDR networks.
 *
 *  The remote address is promoted to a host network (`/32` for IPv4,
 *  `/128` for IPv6) and tested for subnet-of or equality against each
 *  entry in the corresponding address-family list. IPv4 and IPv6 are
 *  kept in separate vectors to prevent cross-family confusion.
 *
 *  Used for both `admin_nets` and `secure_gateway_nets` checks.
 *
 *  @param remoteIp  The address of the connecting client.
 *  @param nets4     Configured IPv4 CIDR blocks to match against.
 *  @param nets6     Configured IPv6 CIDR blocks to match against.
 *  @return `true` if `remoteIp` is within any of the supplied networks.
 */
bool
ipAllowed(
    beast::IP::Address const& remoteIp,
    std::vector<boost::asio::ip::network_v4> const& nets4,
    std::vector<boost::asio::ip::network_v6> const& nets6);

/** Extract the originating client IP from proxy-forwarding HTTP headers.
 *
 *  Tries two header standards in priority order:
 *  1. RFC 7239 `Forwarded` — locates the first (case-insensitive) `for=`
 *     token and parses the IP out of its value.
 *  2. De-facto `X-Forwarded-For` — takes the first comma-delimited entry.
 *
 *  The extracted field is then cleaned: leading/trailing whitespace is
 *  stripped, optional surrounding double-quotes are removed, IPv6 literals
 *  wrapped in `[...]` are unwrapped, and any appended port suffix is
 *  stripped from IPv4 addresses.
 *
 *  @param request  The inbound HTTP request.
 *  @return A `string_view` into the request buffer containing the bare IP
 *      address string, or an empty `string_view` if no valid forwarded
 *      address could be found.
 *  @note This value is used for per-user rate-limit accounting when the
 *      connection comes through a `PROXY`-role secure_gateway. Both false
 *      negatives (losing the real IP) and false positives (accepting a
 *      spoofed value) have resource-limit consequences; the IP gate in
 *      `requestRole()` must pass before this value is trusted.
 */
std::string_view
forwardedFor(http_request_type const& request);

}  // namespace xrpl
