#pragma once

#include <xrpl/beast/hash/hash_append.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/net/IPAddress.h>

#include <cstdint>
#include <optional>
#include <string>

namespace beast::IP {

using Port = std::uint16_t;

/** A version-independent IP address and port combination. */
class Endpoint
{
public:
    /** Create an unspecified endpoint. */
    Endpoint();

    /** Create an endpoint from the address and optional port. */
    explicit Endpoint(Address addr, Port port = 0);

    /** Create an Endpoint from a string.
        If the port is omitted, the endpoint will have a zero port.
        @return An optional endpoint; will be `std::nullopt` on failure
    */
    static std::optional<Endpoint>
    fromStringChecked(std::string const& s);
    static Endpoint
    fromString(std::string const& s);

    /** Returns a string representing the endpoint. */
    [[nodiscard]] std::string
    toString() const;

    /** Returns the port number on the endpoint. */
    [[nodiscard]] Port
    port() const
    {
        return m_port_;
    }

    /** Returns a new Endpoint with a different port. */
    [[nodiscard]] Endpoint
    atPort(Port port) const
    {
        return Endpoint(m_addr_, port);
    }

    /** Returns the address portion of this endpoint. */
    [[nodiscard]] Address const&
    address() const
    {
        return m_addr_;
    }

    /** Convenience accessors for the address part. */
    /** @{ */
    [[nodiscard]] bool
    isV4() const
    {
        return m_addr_.is_v4();
    }
    [[nodiscard]] bool
    isV6() const
    {
        return m_addr_.is_v6();
    }
    [[nodiscard]] AddressV4
    toV4() const
    {
        return m_addr_.to_v4();
    }
    [[nodiscard]] AddressV6
    toV6() const
    {
        return m_addr_.to_v6();
    }
    /** @} */

    /** Arithmetic comparison. */
    /** @{ */
    friend bool
    operator==(Endpoint const& lhs, Endpoint const& rhs);
    friend bool
    operator<(Endpoint const& lhs, Endpoint const& rhs);

    friend bool
    operator!=(Endpoint const& lhs, Endpoint const& rhs)
    {
        return !(lhs == rhs);
    }
    friend bool
    operator>(Endpoint const& lhs, Endpoint const& rhs)
    {
        return rhs < lhs;
    }
    friend bool
    operator<=(Endpoint const& lhs, Endpoint const& rhs)
    {
        return !(lhs > rhs);
    }
    friend bool
    operator>=(Endpoint const& lhs, Endpoint const& rhs)
    {
        return !(rhs > lhs);
    }
    /** @} */

    template <class Hasher>
    friend void
    hash_append(Hasher& h, Endpoint const& endpoint)
    {
        using ::beast::hash_append;
        hash_append(h, endpoint.m_addr_, endpoint.m_port_);
    }

private:
    Address m_addr_;
    Port m_port_;
};

//------------------------------------------------------------------------------

// Properties

/** Returns `true` if the endpoint is a loopback address. */
inline bool
is_loopback(Endpoint const& endpoint)
{
    return is_loopback(endpoint.address());
}

/** Returns `true` if the endpoint is unspecified. */
inline bool
is_unspecified(Endpoint const& endpoint)
{
    return is_unspecified(endpoint.address());
}

/** Returns `true` if the endpoint is a multicast address. */
inline bool
is_multicast(Endpoint const& endpoint)
{
    return is_multicast(endpoint.address());
}

/** Returns `true` if the endpoint is a private unroutable address. */
inline bool
is_private(Endpoint const& endpoint)
{
    return is_private(endpoint.address());
}

/** Returns `true` if the endpoint is a public routable address. */
inline bool
is_public(Endpoint const& endpoint)
{
    return is_public(endpoint.address());
}

//------------------------------------------------------------------------------

/** Returns the endpoint represented as a string. */
inline std::string
to_string(Endpoint const& endpoint)
{
    return endpoint.toString();
}

/** Output stream conversion. */
template <typename OutputStream>
OutputStream&
operator<<(OutputStream& os, Endpoint const& endpoint)
{
    os << to_string(endpoint);
    return os;
}

/** Input stream conversion. */
std::istream&
operator>>(std::istream& is, Endpoint& endpoint);

}  // namespace beast::IP

//------------------------------------------------------------------------------

namespace std {
/** std::hash support. */
template <>
struct hash<::beast::IP::Endpoint>
{
    hash() = default;

    std::size_t
    operator()(::beast::IP::Endpoint const& endpoint) const
    {
        return ::beast::Uhash<>{}(endpoint);
    }
};
}  // namespace std

namespace boost {
/** boost::hash support. */
template <>
struct hash<::beast::IP::Endpoint>
{
    hash() = default;

    std::size_t
    operator()(::beast::IP::Endpoint const& endpoint) const
    {
        return ::beast::Uhash<>{}(endpoint);
    }
};
}  // namespace boost
