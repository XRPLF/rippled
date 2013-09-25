//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_BASICS_SNTPCLIENT_H_INCLUDED
#define RIPPLE_NET_BASICS_SNTPCLIENT_H_INCLUDED

class SNTPClient : public AsyncService
{
protected:
    explicit SNTPClient (Stoppable& parent);

public:
    static SNTPClient* New (Stoppable& parent);
    virtual ~SNTPClient() { }
    virtual void init (std::vector <std::string> const& servers) = 0;
    virtual void addServer (std::string const& mServer) = 0;
    virtual void queryAll () = 0;
    virtual bool getOffset (int& offset) = 0;
};

#endif
