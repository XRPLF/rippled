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

#ifndef RIPPLE_RPC_MANAGERIMPL_H_INCLUDED
#define RIPPLE_RPC_MANAGERIMPL_H_INCLUDED

namespace ripple {
namespace RPC {

class ManagerImpl : public Manager
{
public:
    // The type of map we use to look up by function name.
    //
    typedef boost::unordered_map <std::string, Handler> MapType;

    //--------------------------------------------------------------------------

    explicit ManagerImpl (Journal journal)
        : m_journal (journal)
    {
    }

    ~ManagerImpl()
    {
    }

    void add (Service& service)
    {
        Handlers const& handlers (service.handlers());

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

}
}

#endif
