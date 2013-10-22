//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================


#ifndef RIPPLE_NET_BASICS_MULTISOCKET_H_INCLUDED
#define RIPPLE_NET_BASICS_MULTISOCKET_H_INCLUDED

/** A Socket that can handshake with multiple protocols.
*/
class MultiSocket : public Socket
{
public:
    // immutable flags
    struct Flag
    {
        enum Bit
        {
            peer = 0,           // no handshaking. remaining flags ignored.
            client_role = 1,    // operate in client role
            server_role = 2,    // operate in server role

            proxy = 4,          // client: will send PROXY handshake
                                // server: PROXY handshake required

            ssl = 8,            // client: will use ssl
                                // server: will allow, but not require ssl

            ssl_required = 16   // client: ignored
                                // server: will require ssl (ignores ssl flag)
        };

        Flag (int flags = 0) noexcept
            : m_flags (flags)
        {
        }

        Flag& operator= (int mask) noexcept
        {
            m_flags = mask;
            return *this;
        }

        bool operator== (Flag const& other) const noexcept
        {
            return m_flags == other.m_flags;
        }
        
        inline bool set (int mask) const noexcept
        {
            return (m_flags & mask) == mask;
        }

        inline bool any_set (int mask) const noexcept
        {
            return (m_flags & mask) != 0;
        }

        Flag with (int mask) const noexcept
        {
            return Flag (m_flags | mask);
        }

        Flag without (int mask) const noexcept
        {
            return Flag (m_flags & ~mask);
        }

        int asBits () const noexcept
        {
            return m_flags;
        }

    private:
        int m_flags;
    };
 
    enum Flags
    {
        none = 0,
        client_ssl = 1,
        server_ssl = 2,
        server_ssl_required = 4,
        server_proxy = 8
    };

    typedef HandshakeDetectLogicPROXY::ProxyInfo ProxyInfo;

    // Note that this returns the original flags
    virtual Flag getFlags () = 0;

    virtual IPAddress local_endpoint() = 0;
    virtual IPAddress remote_endpoint() = 0;
    virtual ProxyInfo getProxyInfo () = 0;

    virtual SSL* native_handle () = 0;

    static MultiSocket* New (
        boost::asio::io_service& io_service,
            boost::asio::ssl::context& ssl_context,
                int flags = 0);
};

#endif
