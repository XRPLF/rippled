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

#ifndef BEAST_CORE_SHAREDFUNCTION_H_INCLUDED
#define BEAST_CORE_SHAREDFUNCTION_H_INCLUDED

/** A reference counted, abstract function object.
*/
template <typename Signature, class Allocator = std::allocator <char> >
class SharedFunction;

//------------------------------------------------------------------------------

// nullary function
//
template <typename R, class A>
class SharedFunction <R (void), A>
{
public:
    class Call : public SharedObject
    {
    public:
        virtual R operator() () = 0;
    };

    template <typename F>
    class CallType : public Call
    {
    public:
        typedef typename A:: template rebind <CallType <F> >::other Allocator;

        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f))
            , m_a (a)
        {
        }

        R operator() ()
        {
            return (m_f)();
        }

    private:
        F m_f;
        Allocator m_a;
    };

    //--------------------------------------------------------------------------

    SharedFunction ()
    {
    }

    template <typename F>
    SharedFunction (F f, A a = A ())
        : m_ptr (new (
            typename CallType <F>::Allocator (a)
                .allocate (sizeof (CallType <F>)))
                    CallType <F> (BEAST_MOVE_CAST(F)(f), a))
    {
    }

    SharedFunction (SharedFunction const& other)
        : m_ptr (other.m_ptr)
    {
    }

    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
    {
    }

    SharedFunction& operator= (SharedFunction const& other)
    {
        m_ptr = other.m_ptr;
        return *this;
    }

    bool empty () const
    {
        return m_ptr == nullptr;
    }

    R operator() () const
    {
        bassert (! empty());

        return (*m_ptr)();
    }

private:
    SharedPtr <Call> m_ptr;
};

//------------------------------------------------------------------------------

// unary function (arity 1)
//
template <typename R, typename P1, class A>
class SharedFunction <R (P1), A>
{
public:
    class Call : public SharedObject
    {
    public:
        virtual R operator() (P1 p1) = 0;
    };

    template <typename F>
    class CallType : public Call
    {
    public:
        typedef typename A:: template rebind <CallType <F> >::other Allocator;

        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f))
            , m_a (a)
        {
        }

        R operator() (P1 p1)
        {
            return (m_f)(p1);
        }

    private:
        F m_f;
        Allocator m_a;
    };

    //--------------------------------------------------------------------------

    SharedFunction ()
    {
    }

    template <typename F>
    SharedFunction (F f, A a = A ())
        : m_ptr (new (
            typename CallType <F>::Allocator (a)
                .allocate (sizeof (CallType <F>)))
                    CallType <F> (BEAST_MOVE_CAST(F)(f), a))
    {
    }

    SharedFunction (SharedFunction const& other)
        : m_ptr (other.m_ptr)
    {
    }

    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
    {
    }

    SharedFunction& operator= (SharedFunction const& other)
    {
        m_ptr = other.m_ptr;
        return *this;
    }

    bool empty () const
    {
        return m_ptr == nullptr;
    }

    R operator() (P1 p1) const
    {
        bassert (! empty());

        return (*m_ptr)(p1);
    }

private:
    SharedPtr <Call> m_ptr;
};


#endif
