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

// VFALCO NOTE this looks like some facility for giving websocket
//         a way to produce logging output.
//
namespace websocketpp_02 {
namespace log {

void websocketLog (
    websocketpp_02::log::alevel::value v, std::string const& entry)
{
    using namespace ripple;
    auto isTrace = v == websocketpp_02::log::alevel::DEVEL ||
            v == websocketpp_02::log::alevel::DEBUG_CLOSE;

    WriteLog(isTrace ? lsTRACE : lsDEBUG, WebSocket) << entry;
}

void websocketLog (
    websocketpp_02::log::elevel::value v, std::string const& entry)
{
    using namespace ripple;

    LogSeverity s = lsDEBUG;

    if ((v & websocketpp_02::log::elevel::INFO) != 0)
        s = lsINFO;
    else if ((v & websocketpp_02::log::elevel::FATAL) != 0)
        s = lsFATAL;
    else if ((v & websocketpp_02::log::elevel::RERROR) != 0)
        s = lsERROR;
    else if ((v & websocketpp_02::log::elevel::WARN) != 0)
        s = lsWARNING;

    WriteLog(s, WebSocket) << entry;
}

}
}

// vim:ts=4
