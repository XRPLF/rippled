//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_MACADDRESS_H_INCLUDED
#define BEAST_MACADDRESS_H_INCLUDED

//==============================================================================
/**
    A wrapper for a streaming (TCP) socket.

    This allows low-level use of sockets; for an easier-to-use messaging layer on top of
    sockets, you could also try the InterprocessConnection class.

    @see DatagramSocket, InterprocessConnection, InterprocessConnectionServer
*/
class BEAST_API MACAddress
{
public:
    //==============================================================================
    /** Populates a list of the MAC addresses of all the available network cards. */
    static void findAllAddresses (Array<MACAddress>& results);

    //==============================================================================
    /** Creates a null address (00-00-00-00-00-00). */
    MACAddress();

    /** Creates a copy of another address. */
    MACAddress (const MACAddress& other);

    /** Creates a copy of another address. */
    MACAddress& operator= (const MACAddress& other);

    /** Creates an address from 6 bytes. */
    explicit MACAddress (const uint8 bytes[6]);

    /** Returns a pointer to the 6 bytes that make up this address. */
    const uint8* getBytes() const noexcept        { return address; }

    /** Returns a dash-separated string in the form "11-22-33-44-55-66" */
    String toString() const;

    /** Returns the address in the lower 6 bytes of an int64.

        This uses a little-endian arrangement, with the first byte of the address being
        stored in the least-significant byte of the result value.
    */
    int64 toInt64() const noexcept;

    /** Returns true if this address is null (00-00-00-00-00-00). */
    bool isNull() const noexcept;

    bool operator== (const MACAddress& other) const noexcept;
    bool operator!= (const MACAddress& other) const noexcept;

    //==============================================================================
private:
    uint8 address[6];
};


#endif   // BEAST_MACADDRESS_H_INCLUDED
