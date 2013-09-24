//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_WSDOOR_H_INCLUDED
#define RIPPLE_WSDOOR_H_INCLUDED

/** Handles accepting incoming WebSocket connections. */
class WSDoor : public Stoppable
{
protected:
    explicit WSDoor (Stoppable& parent);

public:
    virtual ~WSDoor () { }

    static WSDoor* New (InfoSub::Source& source, std::string const& strIp,
        int iPort, bool bPublic, boost::asio::ssl::context& ssl_context);
};

#endif
