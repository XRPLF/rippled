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

#ifndef BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTLOGICSSL3_H_INCLUDED
#define BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTLOGICSSL3_H_INCLUDED

// Handshake for SSL 3 (Also TLS 1.0 and 1.1)
//
// http://www.ietf.org/rfc/rfc2246.txt
//
// Section 7.4. Handshake protocol
//
class HandshakeDetectLogicSSL3 : public HandshakeDetectLogic
{
public:
    typedef int arg_type; // dummy

    explicit HandshakeDetectLogicSSL3 (arg_type const&)
    {
    }

    enum
    {
        bytesNeeded = 6
    };

    std::size_t max_needed ()
    {
        return bytesNeeded;
    }

    std::size_t bytes_consumed ()
    {
        return 0;
    }

    template <typename ConstBufferSequence>
    void analyze (ConstBufferSequence const& buffer)
    {
        uint16 version;
        FixedInputBufferSize <bytesNeeded> in (buffer);

        uint8 msg_type;
        if (! in.read (&msg_type))
            return;

        // msg_type must be 0x16 = "SSL Handshake"
        //
        if (msg_type != 0x16)
            return fail ();

        if (! in.read (&version))
            return;
        version = fromNetworkByteOrder (version);

        uint16 length;
        if (! in.read (&length))
            return;

        length = fromNetworkByteOrder (length);

        conclude ();
    }
};

#endif
