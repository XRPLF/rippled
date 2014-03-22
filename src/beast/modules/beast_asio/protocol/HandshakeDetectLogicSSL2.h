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

#ifndef BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTLOGICSSL2_H_INCLUDED
#define BEAST_ASIO_HANDSHAKE_HANDSHAKEDETECTLOGICSSL2_H_INCLUDED

namespace beast {
namespace asio {

// Handshake for SSL 2
//
// http://tools.ietf.org/html/rfc5246#appendix-E.2
//
// std::uint8_t V2CipherSpec[3];
// struct {
//    std::uint16_t msg_length;   
//    std::uint8_t msg_type;
//    Version version;              Should be 'ProtocolVersion'?
//    std::uint16_t cipher_spec_length;
//    std::uint16_t session_id_length;
//    std::uint16_t challenge_length;
//    ...
//
class HandshakeDetectLogicSSL2 : public HandshakeDetectLogic
{
public:
    typedef int arg_type;

    explicit HandshakeDetectLogicSSL2 (arg_type const&)
    {
    }

    enum
    {
        bytesNeeded = 3
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
        FixedInputBufferSize <bytesNeeded> in (buffer);

        {
            std::uint8_t byte;
            if (! in.peek (&byte))
                return;

            // First byte must have the high bit set
            //
            if((byte & 0x80) != 0x80)
            return fail ();
        }

        // The remaining bits contain the
        // length of the following data in bytes.
        //
        std::uint16_t msg_length;
        if (! in.readNetworkInteger(&msg_length))
            return;

        // sizeof (msg_type +
        //         Version (ProtcolVersion?) +
        //         cipher_spec_length +
        //         session_id_length +
        //         challenge_length)
        //
        // Should be 9 or greater.
        //
        if (msg_length < 9)
            return fail ();

        std::uint8_t msg_type;
        if (! in.read (&msg_type))
            return;

        // The msg_type must be 0x01 for a version 2 ClientHello
        //
        if (msg_type != 0x01)
            return fail ();

        conclude ();
    }
};

}
}

#endif
