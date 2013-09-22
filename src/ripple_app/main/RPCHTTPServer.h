//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_APP_RPCHTTPSERVER_H_INCLUDED
#define RIPPLE_APP_RPCHTTPSERVER_H_INCLUDED

class RPCHTTPServer : public Stoppable
{
protected:
    RPCHTTPServer (Stoppable& parent);

public:
    static RPCHTTPServer* New (Stoppable& parent,
        Journal journal, NetworkOPs& networkOPs);

    virtual ~RPCHTTPServer () { }

    /** Opens listening ports based on the Config settings. */
    virtual void setup(Journal journal) = 0;
};

#endif
