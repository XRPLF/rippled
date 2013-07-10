//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SSLCONTEXT_H_INCLUDED
#define RIPPLE_SSLCONTEXT_H_INCLUDED

namespace basio
{

/** Hides a boost::asio::ssl::context implementation.
*/
class SslContext
{
public:
    static SslContext* New ();

    virtual ~SslContext ();

    operator boost::asio::ssl::context& ();

private:
    SslContext ();

private:
    beast::ScopedPointer <boost::asio::ssl::context> m_impl;
};

}

#endif
