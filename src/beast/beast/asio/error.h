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

#ifndef BEAST_ASIO_ERROR_H_INCLUDED
#define BEAST_ASIO_ERROR_H_INCLUDED

#include <boost/asio.hpp>
#include <boost/asio/ssl/error.hpp>

namespace beast {
namespace asio {

/** Returns `true` if the error code is a SSL "short read." */
inline
bool
is_short_read (boost::system::error_code const& ec)
{
    return (ec.category() == boost::asio::error::get_ssl_category())
        && (ERR_GET_REASON(ec.value()) == SSL_R_SHORT_READ);
}
    
/** Returns a human readable message if the error code is SSL related. */
std::string
asio_message(boost::system::error_code const& ec);

}
}

#endif
