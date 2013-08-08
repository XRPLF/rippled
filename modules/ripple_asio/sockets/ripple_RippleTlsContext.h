//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RIPPLETLSCONTEXT_H_INCLUDED
#define RIPPLE_RIPPLETLSCONTEXT_H_INCLUDED

/** A boost SSL context which is set to Generic SSL/TLS (sslv23).

    This is what Ripple uses for its secure connections. The
    curve parameters are predefined and verified to be secure.

    The context is set to sslv23, Transport Layer Security / General.
    This is primarily used for peer to peer servers that don't care
    about certificates or identity verification.

    Usually you don't instantiate this directly, you will need to derive
    a class and initialize the context in your constructor.

    @see SslContext
*/
class RippleTlsContext : public SslContextBase
{
public:
    static RippleTlsContext* New ();

    static void initBoostContext (BoostContextType& context);
};

#endif
