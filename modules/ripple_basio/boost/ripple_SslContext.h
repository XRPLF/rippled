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

    static void initializeFromFile (
        boost::asio::ssl::context& context,
        std::string key_file,
        std::string cert_file,
        std::string chain_file);

private:
    SslContext ();

private:
    beast::ScopedPointer <boost::asio::ssl::context> m_impl;
};

}

#endif
