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

void SharedHandler::operator() ()
{
    pure_virtual_called (__FILE__, __LINE__);
}

void SharedHandler::operator() (error_code const&)
{
    pure_virtual_called (__FILE__, __LINE__);
}

void SharedHandler::operator() (error_code const&, std::size_t)
{
    pure_virtual_called (__FILE__, __LINE__);
}

void SharedHandler::pure_virtual_called (char const* fileName, int lineNumber)
{
    // These shouldn't be getting called. But since the object returned
    // by most implementations of bind have operator() up to high arity
    // levels, it is not generally possible to write a traits test that
    // works in all scenarios for detecting a particular signature of a
    // handler.
    //
    // We use Throw here so beast has a chance to dump the stack BEFORE
    // the stack is unwound.
    //
    Throw (std::runtime_error ("pure virtual called"), fileName, lineNumber);
}
