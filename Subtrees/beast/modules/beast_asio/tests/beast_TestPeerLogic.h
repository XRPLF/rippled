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

#ifndef BEAST_TESTPEERLOGIC_H_INCLUDED
#define BEAST_TESTPEERLOGIC_H_INCLUDED

/** Interface for implementing the logic part of a peer test.
*/
class TestPeerLogic
    : public TestPeerBasics
    , public Uncopyable
{
public:
    typedef boost::system::error_code error_code;

    explicit TestPeerLogic (Socket& socket)
        : m_socket (socket)
    {
    }

    error_code& error () noexcept
    {
        return m_ec;
    }

    error_code const& error () const noexcept
    {
        return m_ec;
    }

    // also assigns, used for async handlers
    error_code const& error (error_code const& ec) noexcept
    {
        return m_ec = ec;
    }

    Socket& socket () noexcept
    {
        return m_socket;
    }

    virtual Role get_role () const noexcept = 0;

    virtual Model get_model () const noexcept = 0;

    virtual void on_connect ()
    {
        pure_virtual ();
    }

    virtual void on_connect_async (error_code const&)
    {
        pure_virtual ();
    }

protected:
    static void pure_virtual ()
    {
        fatal_error ("A TestPeerLogic function was called incorrectly");
    }

private:
    error_code m_ec;
    Socket& m_socket;
};

#endif
