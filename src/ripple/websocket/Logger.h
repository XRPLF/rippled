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

#ifndef RIPPLED_RIPPLE_WEBSOCKET_LOGGER_H
#define RIPPLED_RIPPLE_WEBSOCKET_LOGGER_H

#include <websocketpp/logger/levels.hpp>
#include <ripple/basics/Log.h>

namespace ripple {
namespace websocket {

using LogLevel = websocketpp::log::level;
enum class LoggerType {error, access};

template <LoggerType>
LogSeverity getSeverity (LogLevel);

template <LoggerType loggerType>
class Logger {
public:
    using Hint = websocketpp::log::channel_type_hint::value;

    explicit Logger (Hint) {}
    Logger (LogLevel, Hint) {}
    void set_channels (LogLevel) {}
    void clear_channels (LogLevel) {}

    void write (LogLevel level, std::string const& s)
    {
        WriteLog (getSeverity <loggerType> (level), WebSocket) << s;
    }

    void write (LogLevel level, const char* s)
    {
        write (level, std::string (s));
    }

    bool static_test (LogLevel) const {
        return true;
    }

    bool dynamic_test (LogLevel) {
        return true;
    }
};

template <>
LogSeverity getSeverity <LoggerType::error> (LogLevel level)
{
    if (level & websocketpp::log::elevel::info)
        return lsINFO;
    if (level & websocketpp::log::elevel::fatal)
        return lsFATAL;
    if (level & websocketpp::log::elevel::rerror)
        return lsERROR;
    if (level & websocketpp::log::elevel::warn)
        return lsWARNING;
    return lsDEBUG;
}

template <>
LogSeverity getSeverity <LoggerType::access> (LogLevel level)
{
    auto isTrace = level == websocketpp::log::alevel::devel ||
            level == websocketpp::log::alevel::debug_close;

    return isTrace ? lsTRACE : lsDEBUG;
}

} // websocket
} // ripple

#endif
