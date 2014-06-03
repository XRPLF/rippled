//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTLOGICPROXY_H_INCLUDED
#define BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTLOGICPROXY_H_INCLUDED

#include <beast/module/asio/protocol/HandshakeDetectLogic.h>
#include <beast/module/asio/protocol/InputParser.h>

namespace beast {
namespace asio {

/** Handshake detector for the PROXY protcol

    http://haproxy.1wt.eu/download/1.5/doc/proxy-protocol.txt
*/
class HandshakeDetectLogicPROXY : public HandshakeDetectLogic
{
public:
    typedef int arg_type;

    enum
    {
        // This is for version 1. The largest number of bytes
        // that we could possibly need to parse a valid handshake.
        // We will reject it much sooner if there's an illegal value.
        //
        maxBytesNeeded = 107 // including CRLF, no null term
    };

    struct ProxyInfo
    {
        typedef InputParser::IPv4Address IPv4Address;

        String protocol; // "TCP4", "TCP6", "UNKNOWN"

        IPv4Address sourceAddress;
        IPv4Address destAddress;

        std::uint16_t sourcePort;
        std::uint16_t destPort;
    };

    explicit HandshakeDetectLogicPROXY (arg_type const&)
        : m_consumed (0)
    {
    }

    ProxyInfo const& getInfo () const noexcept
    {
        return m_info;
    }

    std::size_t max_needed ()
    {
        return maxBytesNeeded;
    }

    std::size_t bytes_consumed ()
    {
        return m_consumed;
    }

    template <typename ConstBufferSequence>
    void analyze (ConstBufferSequence const& buffer)
    {
        FixedInputBufferSize <maxBytesNeeded> in (buffer);

        InputParser::State state;

        analyze_input (in, state);

        if (state.passed ())
        {
            m_consumed = in.used ();
            conclude (true);
        }
        else if (state.failed ())
        {
            conclude (false);
        }
    }

    void analyze_input (FixedInputBuffer& in, InputParser::State& state)
    {
        using namespace InputParser;

        if (! match (in, "PROXY ", state))
            return;

        if (match (in, "TCP4 "))
        {
            m_info.protocol = "TCP4";

            if (! read (in, m_info.sourceAddress, state))
                return;

            if (! match (in, " ", state))
                return;

            if (! read (in, m_info.destAddress, state))
                return;

            if (! match (in, " ", state))
                return;

            UInt16Str sourcePort;
            if (! read (in, sourcePort, state))
                return;
            m_info.sourcePort = sourcePort.value;

            if (! match (in, " ", state))
                return;

            UInt16Str destPort;
            if (! read (in, destPort, state))
                return;
            m_info.destPort = destPort.value;

            if (! match (in, "\r\n", state))
                return;

            state = State::pass;
            return;
        }
        else if (match (in, "TCP6 "))
        {
            m_info.protocol = "TCP6";

            state = State::fail;
            return;
        }
        else if (match (in, "UNKNOWN "))
        {
            m_info.protocol = "UNKNOWN";

            state = State::fail;
            return;
        }

        state = State::fail;
    }

private:
    std::size_t m_consumed;
    ProxyInfo m_info;
};

}
}

#endif
