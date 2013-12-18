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

#ifndef BEAST_ASIO_SOCKETS_SOCKETBASE_H_INCLUDED
#define BEAST_ASIO_SOCKETS_SOCKETBASE_H_INCLUDED

/** Common implementation details for Socket and related classes.
    Normally you wont need to use this.
*/
struct SocketBase
{
public:
    typedef boost::system::error_code error_code;

    /** The error returned when a pure virtual is called.
        This is mostly academic since a pure virtual call generates
        a fatal error but in case that gets disabled, this will at
        least return a suitable error code.
    */
    static error_code pure_virtual_error ();

    /** Convenience for taking a reference and returning the error_code. */
    static error_code pure_virtual_error (error_code& ec,
        char const* fileName, int lineNumber);

    /** Called when a function doesn't support the interface. */
    static void pure_virtual_called (char const* fileName, int lineNumber);

    /** Called when synchronous functions without error parameters get an error. */
    static void throw_error (error_code const& ec, char const* fileName, int lineNumber);
};

#endif
