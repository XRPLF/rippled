//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RPC_SERVCE_H_INCLUDED
#define RIPPLE_RPC_SERVCE_H_INCLUDED

#include "Handler.h"

namespace ripple {
namespace RPC {

/** Interface for abstacting RPC commands processing. */
class Service : public Uncopyable
{
public:
    /** Create the service.
        Derived classes will usually call add() repeatedly from their
        constructor to fill in the list of handlers prior to Manager::add.
    */
    Service ();

    virtual ~Service ();

    /** Returns the handlers associated with this service. */
    Handlers const& handlers() const;

    /** Add a handler for the specified method.
        Adding a handler after the service is already associated with a
        Manager results in undefined behavior.
        Thread safety:
            May not be called concurrently.
    */
    template <typename Function>
    void addRPCHandler (std::string const& method, Function function)
    {
        m_handlers.push_back (Handler (method, function));
    }

private:
    Handlers m_handlers;
};

}
}

#endif
