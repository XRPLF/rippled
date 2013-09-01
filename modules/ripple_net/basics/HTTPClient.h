//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_BASICS_HTTPCLIENT_H_INCLUDED
#define RIPPLE_NET_BASICS_HTTPCLIENT_H_INCLUDED

/** Provides an asynchronous HTTP client implementation with optional SSL.
*/
class HTTPClient
{
public:
    enum
    {
        maxClientHeaderBytes = 32 * 1024
    };

    static void get (
        bool bSSL,
        boost::asio::io_service& io_service,
        std::deque <std::string> deqSites,
        const unsigned short port,
        const std::string& strPath,
        std::size_t responseMax,
        boost::posix_time::time_duration timeout,
        FUNCTION_TYPE <bool (const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

    static void get (
        bool bSSL,
        boost::asio::io_service& io_service,
        std::string strSite,
        const unsigned short port,
        const std::string& strPath,
        std::size_t responseMax,
        boost::posix_time::time_duration timeout,
        FUNCTION_TYPE <bool (const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

    static void request (
        bool bSSL,
        boost::asio::io_service& io_service,
        std::string strSite,
        const unsigned short port,
        FUNCTION_TYPE <void (boost::asio::streambuf& sb, const std::string& strHost)> build,
        std::size_t responseMax,
        boost::posix_time::time_duration timeout,
        FUNCTION_TYPE <bool (const boost::system::error_code& ecResult, int iStatus, const std::string& strData)> complete);

    enum
    {
        smsTimeoutSeconds = 30
    };

    static void sendSMS (boost::asio::io_service& io_service, const std::string& strText);
};

#endif
