/** @file
 *  Implements `beast::IP::Endpoint` — parsing, formatting, ordering, and
 *  stream I/O for IP address + port pairs used throughout the XRP Ledger
 *  peer-to-peer stack.
 */

#include <xrpl/beast/net/IPEndpoint.h>

#include <xrpl/beast/net/IPAddress.h>

#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/system/detail/error_code.hpp>

#include <cctype>
#include <ios>
#include <istream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace beast::IP {

/** Constructs an unspecified endpoint with address unspecified and port 0. */
Endpoint::Endpoint() : port_(0)
{
}

/** Constructs an endpoint from an address and an optional port.
 *
 *  @param addr The IP address (v4 or v6).
 *  @param port The port number; defaults to 0 when omitted.
 */
Endpoint::Endpoint(Address addr, Port port) : addr_(std::move(addr)), port_(port)
{
}

/** Parses an endpoint from a string, returning `std::nullopt` on failure.
 *
 *  Inputs longer than 64 characters are rejected immediately as a cheap
 *  denial-of-service guard. Leading and trailing whitespace is trimmed before
 *  parsing. The entire input must be consumed — trailing garbage causes
 *  failure. Accepts IPv4 (`1.2.3.4:80`), bare IPv6
 *  (`2001:db8::1 80`), and bracketed IPv6 (`[::1]:8080`) forms.
 *
 *  @param s The string to parse.
 *  @return The parsed `Endpoint`, or `std::nullopt` if parsing failed.
 *  @note Prefer this over `fromString` when distinguishing a parse failure
 *      from a zero-port, unspecified address is important.
 */
std::optional<Endpoint>
Endpoint::fromStringChecked(std::string const& s)
{
    if (s.size() <= 64)
    {
        std::stringstream is(boost::trim_copy(s));
        Endpoint endpoint;
        is >> endpoint;
        if (!is.fail() && is.rdbuf()->in_avail() == 0)
            return endpoint;
    }
    return {};
}

/** Parses an endpoint from a string, returning a default endpoint on failure.
 *
 *  Convenience wrapper around `fromStringChecked`. On any parse error, returns
 *  a default-constructed `Endpoint` (unspecified address, port 0) rather than
 *  `std::nullopt`. Use `fromStringChecked` when the caller must distinguish a
 *  failed parse from a legitimately zero-port address.
 *
 *  @param s The string to parse.
 *  @return The parsed `Endpoint`, or a default `Endpoint` on failure.
 */
Endpoint
Endpoint::fromString(std::string const& s)
{
    if (std::optional<Endpoint> const result = fromStringChecked(s))
        return *result;
    return Endpoint{};
}

/** Serialises the endpoint to a string in RFC 5952-compatible form.
 *
 *  IPv6 addresses with a non-zero port are enclosed in brackets:
 *  `[2001:db8::1]:8080`. IPv4 addresses use `addr:port`. When port is 0
 *  the port suffix is omitted entirely. String capacity is pre-reserved to
 *  avoid reallocation for the common case.
 *
 *  @return A human-readable string that round-trips through `fromStringChecked`.
 */
std::string
Endpoint::toString() const
{
    std::string s;
    s.reserve(
        (address().is_v6() ? INET6_ADDRSTRLEN - 1 : 15) +
        (port() == 0 ? 0 : 6 + (address().is_v6() ? 2 : 0)));

    if (port() != 0 && address().is_v6())
        s += '[';
    s += address().to_string();
    if (port() != 0u)
    {
        if (address().is_v6())
            s += ']';
        s += ":" + std::to_string(port());
    }

    return s;
}

/** Returns `true` if both address and port are identical. */
bool
operator==(Endpoint const& lhs, Endpoint const& rhs)
{
    return lhs.address() == rhs.address() && lhs.port() == rhs.port();
}

/** Lexicographic ordering: address first, port as tiebreaker.
 *
 *  Provides a total order consistent with `operator==`, enabling `Endpoint`
 *  as a key in `std::set` or `std::map`. The header derives `!=`, `>`,
 *  `<=`, and `>=` from this operator and `operator==`.
 */
bool
operator<(Endpoint const& lhs, Endpoint const& rhs)
{
    if (lhs.address() < rhs.address())
        return true;
    if (lhs.address() > rhs.address())
        return false;
    return lhs.port() < rhs.port();
}

//------------------------------------------------------------------------------

/** Extracts an `Endpoint` from a character stream.
 *
 *  Accepts three wire formats:
 *  - IPv4 with port: `1.2.3.4:80`
 *  - Bracketed IPv6 with port: `[2001:db8::1]:8080`
 *  - Bare IPv6 with port (legacy): `2001:db8::1 8080`
 *
 *  The parser peeks at the first character to choose a branch: `[` triggers
 *  bracketed-IPv6 mode; otherwise the address/port boundary is inferred
 *  lazily — the first `.` signals IPv4 (colon-separated port), and the first
 *  `:` signals bare IPv6 (space-separated port, legacy format). Only the
 *  ASCII character set `[0-9]`, `[a-f]`, `[A-F]`, `.`, and `:` is accepted
 *  inside the address portion; any other character causes `failbit` to be
 *  set and the offending character to be put back. Address length is capped
 *  at `INET6_ADDRSTRLEN` characters. The final address string is validated
 *  via `boost::asio::ip::make_address`; the port (when present) is read with
 *  the stream's own integer extractor. Failures propagate via `failbit`; no
 *  exceptions are thrown.
 *
 *  @note A space character is accepted as the address/port separator for
 *      backward compatibility with a legacy stored-peer-data format.
 *      `fromStringChecked` additionally verifies the whole input was consumed,
 *      so callers should prefer that wrapper over this operator directly.
 *
 *  @param is       The input stream to read from.
 *  @param endpoint The `Endpoint` to populate on success.
 *  @return `is`, with `failbit` set on any parse error.
 */
std::istream&
operator>>(std::istream& is, Endpoint& endpoint)
{
    std::string addrStr;
    // Reserve INET6_ADDRSTRLEN to accommodate the longest valid address; one
    // extra byte lets the length check below detect an overlong input before
    // committing it to addrStr.
    addrStr.reserve(INET6_ADDRSTRLEN);
    char i{0};
    char readTo{0};
    is.get(i);
    if (i == '[')
    {
        readTo = ']';
    }
    else
    {
        addrStr += i;
    }

    while (is && is.rdbuf()->in_avail() > 0 && is.get(i))
    {
        if ((isspace(static_cast<unsigned char>(i)) != 0) || ((readTo != 0) && i == readTo))
            break;

        if ((i == '.') || (i >= '0' && i <= ':') || (i >= 'a' && i <= 'f') ||
            (i >= 'A' && i <= 'F'))
        {
            addrStr += i;

            if (addrStr.size() == INET6_ADDRSTRLEN || (readTo == ':' && addrStr.size() > 15))
            {
                is.setstate(std::ios_base::failbit);
                return is;
            }

            if ((readTo == 0) && (i == '.' || i == ':'))
            {
                readTo = (i == '.') ? ':' : ' ';
            }
        }
        else  // invalid char
        {
            is.unget();
            is.setstate(std::ios_base::failbit);
            return is;
        }
    }

    if (readTo == ']' && is.rdbuf()->in_avail() > 0)
    {
        is.get(i);
        if ((isspace(static_cast<unsigned char>(i)) == 0) && i != ':')
        {
            is.unget();
            is.setstate(std::ios_base::failbit);
            return is;
        }
    }

    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(addrStr, ec);
    if (ec)
    {
        is.setstate(std::ios_base::failbit);
        return is;
    }

    if (is.rdbuf()->in_avail() > 0)
    {
        Port port = 0;
        is >> port;
        if (is.fail())
            return is;
        endpoint = Endpoint(addr, port);
    }
    else
    {
        endpoint = Endpoint(addr);
    }

    return is;
}

}  // namespace beast::IP
