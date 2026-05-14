/** @file
 *  RPC access-control and role assignment.
 *
 *  Implements the privilege classification pipeline that runs at the entry
 *  point of every inbound HTTP and WebSocket RPC connection. Callers obtain
 *  a `Role` via `requestRole()` and a `Resource::Consumer` via
 *  `requestInboundEndpoint()`; together these two values gate command access
 *  and install throttling before any handler dispatch occurs.
 *
 *  @see Role.h for the public interface and the `Role` enum.
 */
#include <xrpld/rpc/Role.h>

#include <xrpl/beast/net/IPAddress.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/server/Port.h>

#include <boost/asio/ip/impl/network_v4.ipp>
#include <boost/asio/ip/impl/network_v6.ipp>
#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>
#include <boost/beast/http/field.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <vector>

namespace xrpl {

/** Return true if admin credentials are not configured or the request
 *  supplies the correct ones.
 *
 *  Credentials are optional: if neither `port.admin_user` nor
 *  `port.admin_password` is set, the function returns true for any caller
 *  that already passed the IP gate. When either field is set, the caller
 *  must supply matching string values in `params["admin_user"]` and
 *  `params["admin_password"]`. The `isString()` guard prevents a
 *  non-string JSON value from bypassing the comparison via type coercion.
 *
 *  @pre `port.admin_nets_v4` or `port.admin_nets_v6` must be non-empty;
 *      this function is only meaningful after `ipAllowed()` has confirmed
 *      the remote address is on an admin network.
 *  @param port    Port configuration carrying `admin_user` and
 *      `admin_password` credential expectations.
 *  @param params  JSON-RPC request body; `admin_user` and `admin_password`
 *      fields are read as strings if present.
 *  @return `true` if no credentials are required or if both credentials
 *      match the port configuration exactly.
 */
bool
passwordUnrequiredOrSentCorrect(Port const& port, json::Value const& params)
{
    XRPL_ASSERT(
        !(port.admin_nets_v4.empty() && port.admin_nets_v6.empty()),
        "xrpl::passwordUnrequiredOrSentCorrect : non-empty admin nets");
    bool const passwordRequired = (!port.admin_user.empty() || !port.admin_password.empty());

    return !passwordRequired ||
        ((params["admin_password"].isString() &&
          params["admin_password"].asString() == port.admin_password) &&
         (params["admin_user"].isString() && params["admin_user"].asString() == port.admin_user));
}

bool
ipAllowed(
    beast::IP::Address const& remoteIp,
    std::vector<boost::asio::ip::network_v4> const& nets4,
    std::vector<boost::asio::ip::network_v6> const& nets6)
{
    // Promote the remote address to a host-prefix network (/32 or /128)
    // and test is_subnet_of || == against each configured block.
    // The dual check is needed because Boost's is_subnet_of treats two
    // identical /32s as equal-but-not-a-subnet; the || handles that edge
    // case when the admin network is itself configured as a single host.
    std::string addrString = remoteIp.to_string();
    if (remoteIp.is_v4())
    {
        addrString += "/32";
        auto ipNet = boost::asio::ip::make_network_v4(addrString);
        for (auto const& net : nets4)
        {
            if (ipNet.is_subnet_of(net) || ipNet == net)
                return true;
        }
    }
    else
    {
        addrString += "/128";
        auto ipNet = boost::asio::ip::make_network_v6(addrString);
        for (auto const& net : nets6)
        {
            if (ipNet.is_subnet_of(net) || ipNet == net)
                return true;
        }
    }

    return false;
}

/** Return true if the remote address and request credentials together
 *  satisfy the port's admin requirements.
 *
 *  Both conditions must hold: the remote IP must be within an
 *  `admin_nets` CIDR block, and the credentials (if configured) must be
 *  correct. Either check failing alone is sufficient to deny admin access.
 *
 *  @param port      Port configuration with `admin_nets_*` and optional
 *      credential expectations.
 *  @param params    JSON-RPC request body passed through to
 *      `passwordUnrequiredOrSentCorrect()`.
 *  @param remoteIp  Physical address of the connecting client.
 *  @return `true` only when both the IP and credential checks pass.
 */
bool
isAdmin(Port const& port, json::Value const& params, beast::IP::Address const& remoteIp)
{
    return ipAllowed(remoteIp, port.admin_nets_v4, port.admin_nets_v6) &&
        passwordUnrequiredOrSentCorrect(port, params);
}

Role
requestRole(
    Role const& required,
    Port const& port,
    json::Value const& params,
    beast::IP::Endpoint const& remoteIp,
    std::string_view user)
{
    if (isAdmin(port, params, remoteIp.address()))
        return Role::ADMIN;

    if (required == Role::ADMIN)
        return Role::FORBID;

    if (ipAllowed(remoteIp.address(), port.secure_gateway_nets_v4, port.secure_gateway_nets_v6))
    {
        if (!user.empty())
            return Role::IDENTIFIED;
        return Role::PROXY;
    }

    return Role::GUEST;
}

/** Return true if the role is entitled to bypass resource throttling.
 *
 *  `ADMIN` and `IDENTIFIED` connections are unlimited. `PROXY`, `GUEST`,
 *  and `USER` are subject to standard rate limiting regardless of traffic
 *  volume. `FORBID` is always false (it never reaches this check in
 *  practice because the connection is rejected before resource allocation).
 *
 *  @param role The privilege level assigned by `requestRole()`.
 *  @return `true` if `role` is `ADMIN` or `IDENTIFIED`.
 */
bool
isUnlimited(Role const& role)
{
    return role == Role::ADMIN || role == Role::IDENTIFIED;
}

/** Convenience overload that resolves the role before testing unlimited status.
 *
 *  Equivalent to `isUnlimited(requestRole(required, port, params, remoteIp, user))`.
 *  Useful when a caller needs only the boolean and has no other use for the
 *  intermediate `Role` value.
 *
 *  @param required  Minimum role required; forwarded to `requestRole()`.
 *  @param port      Port configuration; forwarded to `requestRole()`.
 *  @param params    Request parameters; forwarded to `requestRole()`.
 *  @param remoteIp  Physical remote endpoint; forwarded to `requestRole()`.
 *  @param user      Forwarded user identity; forwarded to `requestRole()`.
 *  @return `true` if the resolved role is `ADMIN` or `IDENTIFIED`.
 */
bool
isUnlimited(
    Role const& required,
    Port const& port,
    json::Value const& params,
    beast::IP::Endpoint const& remoteIp,
    std::string const& user)
{
    return isUnlimited(requestRole(required, port, params, remoteIp, user));
}

Resource::Consumer
requestInboundEndpoint(
    Resource::Manager& manager,
    beast::IP::Endpoint const& remoteAddress,
    Role const& role,
    std::string_view user,
    std::string_view forwardedFor)
{
    if (isUnlimited(role))
        return manager.newUnlimitedEndpoint(remoteAddress);

    return manager.newInboundEndpoint(remoteAddress, role == Role::PROXY, forwardedFor);
}

/** Extract a bare IP address string from a single forwarded-header field value.
 *
 *  Applies the following normalisation steps in order:
 *  1. Trim leading/trailing ASCII spaces and CRLF.
 *  2. Strip balanced outer double-quotes (RFC 7239 allows quoted-string);
 *     unbalanced quotes yield an empty result.
 *  3. Unwrap IPv6 literals enclosed in square brackets (`[::1]` → `::1`);
 *     an unclosed bracket yields an empty result.
 *  4. Detect IPv6 by scanning for a colon after optional leading hex digits;
 *     if found, return as-is (IPv6 addresses cannot have an appended port
 *     outside of brackets).
 *  5. Strip an appended port number from IPv4 addresses (`1.2.3.4:8080`
 *     → `1.2.3.4`).
 *
 *  Returns an empty `string_view` for any malformed or empty input; this
 *  never throws so a bad header cannot interrupt the role-assignment path.
 *
 *  @param field  A single field value extracted from a `for=` token or an
 *      `X-Forwarded-For` entry — already trimmed of any delimiter suffix.
 *  @return A `string_view` into `field` (no copy) containing only the bare
 *      IP address, or an empty `string_view` on parse failure.
 */
static std::string_view
extractIpAddrFromField(std::string_view field)
{
    auto trim = [](std::string_view str) -> std::string_view {
        std::string_view ret = str;

        if (!ret.empty() && ret.front() == ' ')
        {
            std::size_t const firstNonSpace = ret.find_first_not_of(' ');
            if (firstNonSpace == std::string_view::npos)
                return {};

            ret = ret.substr(firstNonSpace);
        }
        if (!ret.empty())
        {
            if (unsigned char const c = ret.back(); c == ' ' || c == '\r' || c == '\n')
            {
                std::size_t const lastNonSpace = ret.find_last_not_of(" \r\n");
                if (lastNonSpace == std::string_view::npos)
                    return {};

                ret = ret.substr(0, lastNonSpace + 1);
            }
        }
        return ret;
    };

    std::string_view ret = trim(field);
    if (ret.empty())
        return {};

    if (ret.front() == '"')
    {
        ret.remove_prefix(1);
        if (ret.empty() || ret.back() != '"')
            return {};  // Unbalanced double quotes.

        ret.remove_suffix(1);
        ret = trim(ret);
    }
    if (ret.empty())
        return {};

    if (ret.front() == '[')
    {
        ret.remove_prefix(1);

        auto const closeBracket = std::ranges::find_if_not(ret, [](unsigned char c) {
            return std::isxdigit(c) || c == ':' || c == '.' || c == ' ';
        });

        if (closeBracket == ret.end() || (*closeBracket) != ']')
            return {};

        ret = ret.substr(0, closeBracket - ret.begin());
        ret = trim(ret);
    }
    if (ret.empty())
        return {};

    // Detect IPv6: skip leading hex digits; if the next char is a colon the
    // address is IPv6 and cannot have a port appended outside of brackets.
    {
        auto const colon = std::ranges::find_if_not(
            ret, [](unsigned char c) { return std::isxdigit(c) || c == ' '; });

        if (colon == ret.end() || (*colon) == ':')
            return ret;
    }

    // IPv4 with appended port — strip at the colon.
    if (std::size_t const colon = ret.find(':'); colon != std::string_view::npos)
        ret = ret.substr(0, colon);

    return ret;
}

std::string_view
forwardedFor(http_request_type const& request)
{
    // RFC 7239 `Forwarded` takes priority over the legacy `X-Forwarded-For`.
    if (auto it = request.find(boost::beast::http::field::forwarded); it != request.end())
    {
        auto asciiToLower = [](char c) -> char {
            return ((static_cast<unsigned>(c) - 65U) < 26) ? c + 'a' - 'A' : c;
        };

        // Case-insensitive search for the first "for=" token at a directive
        // boundary (start of value, or preceded by , ; or OWS). The boundary
        // check prevents false matches inside addresses or earlier directives.
        static constexpr std::string_view kFOR_STR{"for="};
        auto const atFieldBoundary = [begin = it->value().begin()](auto p) {
            return p == begin || p[-1] == ';' || p[-1] == ',' || p[-1] == ' ' || p[-1] == '\t';
        };
        auto found = it->value().begin();
        while (true)
        {
            found = std::search(
                found,
                it->value().end(),
                kFOR_STR.begin(),
                kFOR_STR.end(),
                [&asciiToLower](char c1, char c2) { return asciiToLower(c1) == asciiToLower(c2); });

            if (found == it->value().end())
                return {};

            if (atFieldBoundary(found))
                break;

            ++found;
        }

        std::advance(found, kFOR_STR.size());

        // Delimit to the first "," or ";" so multi-hop entries don't bleed
        // into the address field; fall back to end-of-header if none found.
        auto const end = it->value().end();
        std::size_t const pos = [&found, &end]() {
            std::size_t const pos =
                std::string_view(found, std::distance(found, end)).find_first_of(",;");
            if (pos != std::string_view::npos)
                return pos;

            return static_cast<std::size_t>(std::distance(found, end));
        }();

        return extractIpAddrFromField({found, pos});
    }

    // Legacy `X-Forwarded-For`: take only the first comma-delimited entry,
    // which by convention is the originating client address.
    if (auto it = request.find("X-Forwarded-For"); it != request.end())
    {
        std::size_t found = it->value().find(',');
        if (found == boost::string_view::npos)
            found = it->value().length();
        return extractIpAddrFromField(it->value().substr(0, found));
    }

    return {};
}

}  // namespace xrpl
