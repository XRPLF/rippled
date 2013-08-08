//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MULTISOCKET_H_INCLUDED
#define RIPPLE_MULTISOCKET_H_INCLUDED

/** A Socket that can handshake with multiple protocols.
*/
class RippleMultiSocket : public Socket
{
public:
    enum Flags
    {
        none = 0,
        client_ssl = 1,
        server_ssl = 2,
        server_ssl_required = 4,
        server_proxy = 8
    };

    struct Options
    {
        Options (Flags flags = none);

        // Always perform SSL handshake as client role
        bool useClientSsl;

        // Enable optional SSL capability as server role
        bool enableServerSsl;

        // Require SSL as server role.
        // Does not require that enableServerSsl is set
        bool requireServerSsl;

        // Require PROXY protocol handshake as server role
        bool requireServerProxy;

    private:
        void setFromFlags (Flags flags);
    };

    static RippleMultiSocket* New (boost::asio::io_service& io_service,
                                   Options const& options = none);
};

#endif
