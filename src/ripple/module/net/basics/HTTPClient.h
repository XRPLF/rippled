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

#ifndef RIPPLE_NET_BASICS_HTTPCLIENT_H_INCLUDED
#define RIPPLE_NET_BASICS_HTTPCLIENT_H_INCLUDED

namespace ripple {

/** Provides an asynchronous HTTP client implementation with optional SSL.
*/
class HTTPClient
{
public:
    enum
    {
        maxClientHeaderBytes = 32 * 1024
    };

    static void initializeSSLContext ();

    static void get (
        bool bSSL,
        boost::asio::io_service& io_service,
        std::deque <std::string> deqSites,
        const unsigned short port,
        const std::string& strPath,
        std::size_t responseMax,
        boost::posix_time::time_duration timeout,
        std::function <bool (const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

    static void get (
        bool bSSL,
        boost::asio::io_service& io_service,
        std::string strSite,
        const unsigned short port,
        const std::string& strPath,
        std::size_t responseMax,
        boost::posix_time::time_duration timeout,
        std::function <bool (const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

    static void request (
        bool bSSL,
        boost::asio::io_service& io_service,
        std::string strSite,
        const unsigned short port,
        std::function <void (boost::asio::streambuf& sb, const std::string& strHost)> build,
        std::size_t responseMax,
        boost::posix_time::time_duration timeout,
        std::function <bool (const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

    enum
    {
        smsTimeoutSeconds = 30
    };

    static void sendSMS (boost::asio::io_service& io_service, const std::string& strText);
};

} // ripple

#endif
