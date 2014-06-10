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

#ifndef RIPPLE_OVERLAY_PEER_PROTOCOL_DETECTOR_H_INCLUDED
#define RIPPLE_OVERLAY_PEER_PROTOCOL_DETECTOR_H_INCLUDED

#include <boost/asio/buffer.hpp>
#include <boost/logic/tribool.hpp>
#include <cstdint>

namespace ripple {

/** Detects the peer protocol handshake. */
class peer_protocol_detector
{
public:
    /** Returns `true` if the buffers contain the required protocol messages.
        The peer protcol requires the 'hello' message as the first item on
        the stream. We check the 6-byte message header to determine if the
        hello is present.
        @return `false` if the buffers cannot possibly contain the message, or
            `boost::indeterminate` if more data is needed.
    */
    template <class ConstBufferSequence>
    boost::tribool
    operator() (ConstBufferSequence const& buffers)
    {
        std::array <std::uint8_t, 6> data;
        std::size_t const n (boost::asio::buffer_copy (data, buffers));
        /*
        Protocol messages are framed by a 6 byte header which includes
        a big-endian 4-byte length followed by a big-endian 2-byte type.
        The type for 'hello' is 1.
        */
        if (n>=1 && data[0] != 0)
            return false;
        if (n>=2 && data[1] != 0)
            return false;
        if (n>=5 && data[4] != 0)
            return false;
        if (n>=6 && data[5] != 1)
            return false;
        if (n>=6)
            return true;
        return boost::indeterminate;
    }
};

} // ripple

#endif
