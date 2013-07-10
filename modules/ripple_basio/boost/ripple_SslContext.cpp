//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace basio
{

SslContext* SslContext::New ()
{
    return new SslContext;
}

SslContext::~SslContext ()
{
}

SslContext::operator boost::asio::ssl::context& ()
{
    return *m_impl;
}

SslContext::SslContext ()
    : m_impl (new boost::asio::ssl::context (boost::asio::ssl::context::sslv23))
{
}

}
