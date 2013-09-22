//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_FRAME_RPCSERVICE_H_INCLUDED
#define RIPPLE_FRAME_RPCSERVICE_H_INCLUDED

#include "../../../beast/beast/utility/Journal.h"

namespace ripple
{

using namespace beast;

/** Interface for abstacting RPC commands processing. */
class RPCService : public Uncopyable
{
public:
    //--------------------------------------------------------------------------

    /** An invokable handler for a particular method. */
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

        Handler (Handler const& other)
            : m_method (other.m_method)
            , m_function (other.m_function)
        {
        }

        Handler& operator= (Handler const& other)
        {
            m_method = other.m_method;
            m_function = other.m_function;
            return *this;
        }

        /** Returns the method called when this handler is invoked. */
        std::string const& method() const
        {
            return m_method;
        }

        /** Synchronously invoke the method on the associated service.
            Thread safety:
                Determined by the owner.
        */
        Json::Value operator() (Json::Value const& args) const
        {
            return m_function (args);
        }

    private:
        std::string m_method;
        SharedFunction <Json::Value (Json::Value const&)> m_function;
    };

    //--------------------------------------------------------------------------

    /** Manages a collection of RPCService interface objects. */
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
        virtual void add (RPCService& service) = 0;

        /** Add a subclass of RPCService and return the original pointer.
            This is provided as a convenient so that RPCService objects may
            be added from ctor-initializer lists.
        */
        template <class Derived>
        Derived* add (Derived* derived)
        {
            add (*(static_cast <RPCService*>(derived)));
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

    //--------------------------------------------------------------------------
public:
    typedef std::vector <Handler> Handlers;

    /** Create the service.
        Derived classes will usually call add() repeatedly from their
        constructor to fill in the list of handlers prior to Manager::add.
    */
    RPCService ();

    virtual ~RPCService ();

    /** Returns the handlers associated with this service. */
    Handlers const& handlers() const
    {
        return m_handlers;
    }

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
    class ManagerImp;

    Handlers m_handlers;
};

//------------------------------------------------------------------------------

}

#endif
