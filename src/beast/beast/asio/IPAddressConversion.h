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

#ifndef BEAST_ASIO_IPADDRESSCONVERSION_H_INCLUDED
#define BEAST_ASIO_IPADDRESSCONVERSION_H_INCLUDED

#include "../net/IPAddress.h"

#include <boost/asio.hpp>

namespace beast {

struct IPAddressConversion
{
    /** Convert to IPAddress.
        The port is set to zero.
    */
    static IPAddress from_asio (boost::asio::ip::address const& address);

    /** Convert to IPAddress, including port. */
    static IPAddress from_asio (boost::asio::ip::tcp::endpoint const& endpoint);

    /** Convert to asio::ip::address.
        The port is ignored.
    */
    static boost::asio::ip::address to_asio_address (IPAddress const& address);

    /** Convert to asio::ip::tcp::endpoint. */
    static boost::asio::ip::tcp::endpoint to_asio_endpoint (IPAddress const& address);
};

}

#endif
