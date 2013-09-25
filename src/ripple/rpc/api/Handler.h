//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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
