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

#ifndef RIPPLE_RPC_HANDLER_H_INCLUDED
#define RIPPLE_RPC_HANDLER_H_INCLUDED

#include <vector>

namespace ripple {
using namespace beast;

namespace RPC {

/** An invokable handler for a particular RPC method. */
class Handler
{
public:
    /** Create a handler with the specified method and function. */
    template <typename Function> // allocator
    Handler (std::string const& method_, Function function)
        : m_method (method_)
        , m_function (function)
    {
    }

    Handler (Handler const& other);
    Handler& operator= (Handler const& other);

    /** Returns the method called when this handler is invoked. */
    std::string const& method() const;

    /** Synchronously invoke the method on the associated service.
        Thread safety:
            Determined by the owner.
    */
    Json::Value operator() (Json::Value const& args) const;

private:
    std::string m_method;
    SharedFunction <Json::Value (Json::Value const&)> m_function;
};

/** The type of container that holds a set of Handler objects. */
typedef std::vector <Handler> Handlers;

}
}

#endif
