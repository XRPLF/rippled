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

#ifndef BEAST_ASIO_BASICS_SSLCONTEXT_H_INCLUDED
#define BEAST_ASIO_BASICS_SSLCONTEXT_H_INCLUDED

#include <boost/asio/ssl/context.hpp>

namespace beast {
namespace asio {

/** Simple base class for passing a context around.
    This lets derived classes hide their implementation from the headers.
*/
class SSLContext : public Uncopyable
{
public:
    virtual ~SSLContext ();

    // Saves typing
    typedef boost::asio::ssl::context ContextType;

    inline ContextType& get () noexcept
    {
        return m_context;
    }

    inline ContextType const& get () const noexcept
    {
        return m_context;
    }

    // implicit conversion
    inline operator ContextType& () noexcept
    {
        return get ();
    }

    inline operator ContextType const& () const noexcept
    {
        return get ();
    }

protected:
    explicit SSLContext (ContextType& context);

    ContextType& m_context;
};

}
}

#endif
