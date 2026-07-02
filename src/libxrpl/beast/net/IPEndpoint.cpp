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

Endpoint::Endpoint() : port_(0)
{
}

Endpoint::Endpoint(Address addr, Port port) : addr_(std::move(addr)), port_(port)
{
}

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

Endpoint
Endpoint::fromString(std::string const& s)
{
    if (std::optional<Endpoint> const result = fromStringChecked(s))
        return *result;
    return Endpoint{};
}

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

bool
operator==(Endpoint const& lhs, Endpoint const& rhs)
{
    return lhs.address() == rhs.address() && lhs.port() == rhs.port();
}

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

std::istream&
operator>>(std::istream& is, Endpoint& endpoint)
{
    std::string addrStr;
    // valid addresses only need INET6_ADDRSTRLEN-1 chars, but allow the extra
    // char to check for invalid lengths
    addrStr.reserve(INET6_ADDRSTRLEN);
    char i{0};
    char readTo{0};
    is.get(i);
    if (i == '[')
    {  // we are an IPv6 endpoint
        readTo = ']';
    }
    else
    {
        addrStr += i;
    }

    while (is && is.rdbuf()->in_avail() > 0 && is.get(i))
    {
        // NOTE: There is a legacy data format
        // that allowed space to be used as address / port separator
        // so we continue to honor that here by assuming we are at the end
        // of the address portion if we hit a space (or the separator
        // we were expecting to see)
        if ((isspace(static_cast<unsigned char>(i)) != 0) || ((readTo != 0) && i == readTo))
            break;

        if ((i == '.') || (i >= '0' && i <= ':') || (i >= 'a' && i <= 'f') ||
            (i >= 'A' && i <= 'F'))
        {
            addrStr += i;

            // don't exceed a reasonable length...
            if (addrStr.size() == INET6_ADDRSTRLEN || (readTo == ':' && addrStr.size() > 15))
            {
                is.setstate(std::ios_base::failbit);
                return is;
            }

            if ((readTo == 0) && (i == '.' || i == ':'))
            {
                // if we see a dot first, must be IPv4
                // otherwise must be non-bracketed IPv6
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
