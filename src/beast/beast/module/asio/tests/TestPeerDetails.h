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

#ifndef BEAST_ASIO_TESTS_TESTPEERDETAILS_H_INCLUDED
#define BEAST_ASIO_TESTS_TESTPEERDETAILS_H_INCLUDED

#include <beast/asio/abstract_socket.h>

namespace beast {
namespace asio {

/** Base class of all detail objects. */
class TestPeerDetails
{
public:
    virtual ~TestPeerDetails () { }

    virtual String name () const = 0;

    virtual abstract_socket& get_socket () = 0;

    virtual abstract_socket& get_acceptor () = 0;

    boost::asio::io_service& get_io_service ()
    {
        return m_io_service;
    }

private:
    boost::asio::io_service m_io_service;
};

}
}

#endif
