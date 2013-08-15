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

#ifndef BEAST_PROXYYHANDSHAKE_H_INCLUDED
#define BEAST_PROXYYHANDSHAKE_H_INCLUDED

/** PROXY protocol handshake state machine.

    The PROXY Protocol:
    http://haproxy.1wt.eu/download/1.5/doc/proxy-protocol.txt
*/
class ProxyHandshake
{
public:
    /** Status of the handshake state machine. */
    enum Status
    {
        statusNone,         // No handshake expected
        statusHandshake,    // Handshake in progress
        statusFailed,       // Handshake failed
        statusOk,           // Handshake succeeded
    };

    enum Proto
    {
        protoTcp4,
        protoTcp6,
        protoUnknown
    };

    /** PROXY information for IPv4 families. */
    struct IPv4
    {
        struct Addr
        {
            int a;
            int b;
            int c;
            int d;

            bool chop (String& input);
        };

        Addr sourceAddr;
        Addr destAddr;
        int sourcePort;
        int destPort;
    };

    /** PROXY information for IPv6 families. */
    struct IPv6
    {
        struct Addr
        {
            int a;
            int b;
            int c;
            int d;
        };

        Addr sourceAddr;
        Addr destAddr;
        int sourcePort;
        int destPort;
    };

    /** Fully decoded PROXY information. */
    struct Endpoints
    {
        Endpoints ()
            : proto (protoUnknown)
        {
        }

        Proto proto;
        IPv4 ipv4;      // valid if proto == protoTcp4
        IPv6 ipv6;      // valid if proto == protoTcp6;
    };

    //--------------------------------------------------------------------------

    /** Parser for PROXY version 1. */
    struct Version1
    {
        enum
        {
            // Maximum input buffer size needed, including a null
            // terminator, as per the PROXY protocol specification.
            maxBufferBytes = 108
        };

        Endpoints endpoints;

        Version1 ();

        /** Parse the header.    
            @param rawHeader a pointer to the header data
            @return `true` If it was parsed successfully.
        */
        bool parse (void const* headerData, size_t headerBytes);
    };

    //--------------------------------------------------------------------------

    /** Create the handshake state.
        If a handshake is expected, then it is required.
        @param wantHandshake `false` to skip handshaking.
    */
    explicit ProxyHandshake (bool expectHandshake = false);

    ~ProxyHandshake ();

    inline Status getStatus () const noexcept
    {
        return m_status;
    }

    inline Endpoints const& getEndpoints () const noexcept
    {
        return m_endpoints;
    };

    /** Feed the handshaking state engine.
        @return The number of bytes consumed in the input buffer.
    */
    std::size_t feed (void const* inputBuffer, std::size_t inputBytes);

    // Utility functions used by parsers
    static int indexOfFirstNonNumber (String const& input);
    static bool chop (String const& what, String& input);
    static bool chopUInt (int* value, int limit, String& input);

private:
    void parseLine ();

private:
    enum
    {
        maxVersion1Bytes = 107      // including crlf, not including null term
    };

    Status m_status;
    String m_buffer;
    bool m_gotCR;
    Endpoints m_endpoints;
};

#endif
