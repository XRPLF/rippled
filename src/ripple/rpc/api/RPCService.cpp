//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple
{

class RPCService::ManagerImp : public RPCService::Manager
{
public:
    // The type of map we use to look up by function name.
    //
    typedef boost::unordered_map <std::string, Handler> MapType;

    //--------------------------------------------------------------------------

    explicit ManagerImp (Journal journal)
        : m_journal (journal)
    {
    }

    ~ManagerImp()
    {
    }

    void add (RPCService& service)
    {
        Handlers const& handlers (service.m_handlers);

        SharedState::Access state (m_state);

        for (Handlers::const_iterator iter (handlers.begin());
            iter != handlers.end(); ++iter)
        {
            Handler const& handler (*iter);
            std::pair <MapType::const_iterator, bool> result (
                state->table.emplace (handler.method(), handler));
            if (!result.second)
                m_journal.error << "duplicate method '" << handler.method() << "'";
        }
    }

    std::pair <bool, Json::Value> call (
        std::string const& method, Json::Value const& args)
    {
        Handler const* handler (find (method));
        if (! handler)
            return std::make_pair (false, Json::Value());
        return std::make_pair (true, (*handler)(args));
    }

    Handler const* find (std::string const& method)
    {
        Handler const* handler (nullptr);
        // Peform lookup on the method to retrieve handler
        SharedState::Access state (m_state);
        MapType::iterator iter (state->table.find (method));
        if (iter != state->table.end())
            handler = &iter->second;
        else
            m_journal.debug << "method '" << method << "' not found.";
        return handler;
    }

private:
    struct State
    {
        MapType table;
    };

    typedef SharedData <State> SharedState;

    Journal m_journal;
    SharedState m_state;
};

//------------------------------------------------------------------------------

RPCService::Manager* RPCService::Manager::New (Journal journal)
{
    return new RPCService::ManagerImp (journal);
}

//------------------------------------------------------------------------------

RPCService::RPCService ()
{
}

RPCService::~RPCService ()
{
}

}
