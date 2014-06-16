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

#ifndef BEAST_ASIO_SHAREDARG_H_INCLUDED
#define BEAST_ASIO_SHAREDARG_H_INCLUDED

namespace beast {
namespace asio {

/** A container that turns T into a SharedObject.
    We use this to manage the lifetime of objects passed to handlers.
*/
template <typename T>
struct SharedArg
{
private:
    struct Arg : SharedObject
    {
        Arg ()
        {
        }

        explicit Arg (BEAST_MOVE_ARG(T) t)
            : value (BEAST_MOVE_CAST(T)(t))
        {
        }

        template <class P1>
        explicit Arg (P1 p1)
            : value (p1)
        {
        }

        template <class P1, class P2>
        Arg (P1 p1, P2 p2)
            : value (p1, p2)
        {
        }

        template <class P1, class P2, class P3>
        Arg (P1 p1, P2 p2, P3 p3)
            : value (p1, p2, p3)
        {
        }

        template <class P1, class P2, class P3, class P4>
        Arg (P1 p1, P2 p2, P3 p3, P4 p4)
            : value (p1, p2, p3, p4)
        {
        }

        ~Arg ()
        {
        }

        T value;
    };

public:
    SharedArg ()
    {
    }

    explicit SharedArg (BEAST_MOVE_ARG(T) t)
        : m_arg (new Arg (BEAST_MOVE_CAST(T)(t)))
    {
    }

    template <class P1>
    explicit SharedArg (P1 p1)
        : m_arg (new Arg (p1))
    {
    }

    template <class P1, class P2>
    SharedArg (P1 p1, P2 p2)
        : m_arg (new Arg (p1, p2))
    {
    }

    template <class P1, class P2, class P3>
    SharedArg (P1 p1, P2 p2, P3 p3)
        : m_arg (new Arg (p1, p2, p3))
    {
    }

    template <class P1, class P2, class P3, class P4>
    SharedArg (P1 p1, P2 p2, P3 p3, P4 p4)
        : m_arg (new Arg (p1, p2, p3, p4))
    {
    }

    SharedArg (SharedArg const& other)
        : m_arg (other.m_arg)
    {
    }

    SharedArg& operator= (SharedArg const& other)
    {
        m_arg = other.m_arg;
        return *this;
    }

    T& get ()
    {
        return m_arg->value;
    }

    T const& get () const
    {
        return m_arg->value;
    }

    T& operator* ()
    {
        return get();
    }

    T const& operator* () const
    {
        return get();
    }

    T* operator-> ()
    {
        return &get();
    }

    T const* operator-> () const
    {
        return &get();
    }

    operator T& ()
    {
        return m_arg->value;
    }

    operator T const& () const
    {
        return m_arg->value;
    }

private:
    SharedPtr <Arg> m_arg;
};

}
}

#endif
