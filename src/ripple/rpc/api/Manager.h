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
