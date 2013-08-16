//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MULTISOCKET_H_INCLUDED
#define RIPPLE_MULTISOCKET_H_INCLUDED

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

        Flag (int flags) noexcept
            : m_flags (flags)
        {
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

    static MultiSocket* New (boost::asio::io_service& io_service, int flags = 0);

    // Ripple uses a SSL/TLS context with specific parameters and this returns
    // a reference to the corresponding boost::asio::ssl::context object.
    //
    static SslContextBase::BoostContextType& getRippleTlsBoostContext ();
};

#endif
