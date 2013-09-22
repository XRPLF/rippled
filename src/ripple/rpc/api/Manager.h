//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RPC_MANAGER_H_INCLUDED
#define RIPPLE_RPC_MANAGER_H_INCLUDED

#include "../../../beast/beast/utility/Journal.h"

#include "Handler.h"
#include "Service.h"

namespace ripple {
using namespace beast;

namespace RPC {

/** Manages a collection of Service interface objects. */
class Manager
{
public:
    static Manager* New (Journal journal);

    virtual ~Manager() { }

    /** Add a service.
        The list of commands that the service handles is enumerated and
        added to the manager's dispatch table.
        Thread safety:
            Safe to call from any thread.
            May only be called once for a given service.
    */
    virtual void add (Service& service) = 0;

    /** Add a subclass of Service and return the original pointer.
        This is provided as a convenient so that RPCService objects may
        be added from ctor-initializer lists.
    */
    template <class Derived>
    Derived* add (Derived* derived)
    {
        add (*(static_cast <Service*>(derived)));
        return derived;
    }

    /** Execute an RPC command synchronously.
        On return, if result.first == `true` then result.second will
        have the Json return value from the call of the handler.
    */
    virtual std::pair <bool, Json::Value> call (
        std::string const& method, Json::Value const& args) = 0;

    /** Returns the Handler for the specified method, or nullptr.
        Thread safety:
            Safe to call from any threads.
    */
    virtual Handler const* find (std::string const& method) = 0;
};

}
}

#endif
