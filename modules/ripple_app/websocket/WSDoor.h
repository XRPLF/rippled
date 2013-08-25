//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_WSDOOR_RIPPLEHEADER
#define RIPPLE_WSDOOR_RIPPLEHEADER

/** Handles accepting incoming WebSocket connections. */
class WSDoor
{
public:
    static WSDoor* New (std::string const& strIp, int iPort, bool bPublic,
        boost::asio::ssl::context& ssl_context);
};

#endif
