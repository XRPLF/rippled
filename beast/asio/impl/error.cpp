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

#include <beast/asio/error.h>
#include <boost/lexical_cast.hpp>

namespace beast {
namespace asio {

// This buffer must be at least 120 bytes, most examples use 256.
// https://www.openssl.org/docs/crypto/ERR_error_string.html
static std::uint32_t const errorBufferSize (256);

std::string
asio_message (boost::system::error_code const& ec)
{
    std::string error;

    if (ec.category () == boost::asio::error::get_ssl_category ())
    {
        error = " ("
            + boost::lexical_cast<std::string> (ERR_GET_LIB (ec.value ()))
            + ","
            + boost::lexical_cast<std::string> (ERR_GET_FUNC (ec.value ()))
            + ","
            + boost::lexical_cast<std::string> (ERR_GET_REASON (ec.value ()))
            + ") ";
        
        // 
        char buf[errorBufferSize];
        ::ERR_error_string_n (ec.value (), buf, errorBufferSize);
        error += buf;
    }
    else
    {
        error = ec.message ();
    }

    return error;
}

}
}
