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

#ifndef BEAST_ASIO_TESTS_TESTPEERLOGIC_H_INCLUDED
#define BEAST_ASIO_TESTS_TESTPEERLOGIC_H_INCLUDED

namespace beast {
namespace asio {

/** Interface for implementing the logic part of a peer test. */
class TestPeerLogic : public TestPeerBasics
{
public:
    typedef boost::system::error_code error_code;

    explicit TestPeerLogic (abstract_socket& socket);

    error_code& error () noexcept;
    error_code const& error () const noexcept;
    error_code const& error (error_code const& ec) noexcept; // assigns to m_ec

    abstract_socket& socket () noexcept;

    virtual PeerRole get_role () const noexcept = 0;
    
    virtual Model get_model () const noexcept = 0;

    virtual void on_connect ();
    
    virtual void on_connect_async (error_code const&);

    // asynchronous logic classes
    // must call this when they are done
    virtual void finished ();

    static void pure_virtual ();

private:
    error_code m_ec;
    abstract_socket* m_socket;
};

}
}

#endif
