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

/** A reference counted, abstract function object. */
template <typename Signature, class Allocator = std::allocator <char> >
class SharedFunction;

//------------------------------------------------------------------------------

template <class R, class A>
class SharedFunction <R (void), A>
{
public:
    struct Call : SharedObject
        { virtual R operator() () = 0; };

    template <typename F>
    struct CallType : Call
    {
        typedef typename A:: template rebind <CallType <F> >::other Allocator;
        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f)), m_a (a)
            { }
        R operator() ()
            { return (m_f)(); }
    private:
        F m_f;
        Allocator m_a;
    };

    typedef R result_type;
    template <typename F>
    explicit SharedFunction (F f, A a = A ())
        : m_ptr (new (typename CallType <F>::Allocator (a).allocate (1))
            CallType <F> (BEAST_MOVE_CAST(F)(f), a))
        { }
    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
        { }
    SharedFunction ()
        { }
    bool empty () const
        { return m_ptr == nullptr; }
    R operator() () const
        { return (*m_ptr)(); }

private:
    SharedPtr <Call> m_ptr;
};

//------------------------------------------------------------------------------

template <class R, class P1, class A>
class SharedFunction <R (P1), A>
{
public:
    struct Call : public SharedObject
        { virtual R operator() (P1 const& p1) = 0; };

    template <typename F>
    struct CallType : Call
    {
        typedef typename A:: template rebind <CallType <F> >::other Allocator;
        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f)), m_a (a)
            { }
        R operator() (P1 const& p1)
            { return (m_f)(p1); }
    private:
        F m_f;
        Allocator m_a;
    };

    typedef R result_type;
    template <typename F>
    SharedFunction (F f, A a = A ())
        : m_ptr (new (typename CallType <F>::Allocator (a).allocate (1))
            CallType <F> (BEAST_MOVE_CAST(F)(f), a))
        { }
    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
        { }
    SharedFunction ()
        { }
    bool empty () const
        { return m_ptr == nullptr; }
    R operator() (P1 const& p1) const
        { return (*m_ptr)(p1); }

private:
    SharedPtr <Call> m_ptr;
};

//------------------------------------------------------------------------------

template <class R, class P1, class P2, class A>
class SharedFunction <R (P1, P2), A>
{
public:
    struct Call : public SharedObject
        { virtual R operator() (P1 const& p1, P2 const& p2) = 0; };

    template <typename F>
    struct CallType : Call
    {
        typedef typename A:: template rebind <CallType <F> >::other Allocator;
        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f)), m_a (a)
            { }
        R operator() (P1 const& p1, P2 const& p2)
            { return (m_f)(p1, p2); }
    private:
        F m_f;
        Allocator m_a;
    };

    typedef R result_type;
    template <typename F>
    SharedFunction (F f, A a = A ())
        : m_ptr (new (typename CallType <F>::Allocator (a).allocate (1))
            CallType <F> (BEAST_MOVE_CAST(F)(f), a))
        { }
    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
        { }
    SharedFunction ()
        { }
    bool empty () const
        { return m_ptr == nullptr; }
    R operator() (P1 const& p1, P2 const& p2) const
        { return (*m_ptr)(p1, p2); }

private:
    SharedPtr <Call> m_ptr;
};

//------------------------------------------------------------------------------

template <class R, class P1, class P2, class P3, class A>
class SharedFunction <R (P1, P2, P3), A>
{
public:
    struct Call : public SharedObject
        { virtual R operator() (P1 const& p1, P2 const& p2, P3 const& p3) = 0; };

    template <typename F>
    struct CallType : Call
    {
        typedef typename A:: template rebind <CallType <F> >::other Allocator;
        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f)), m_a (a)
            { }
        R operator() (P1 const& p1, P2 const& p2, P3 const& p3)
            { return (m_f)(p1, p2, p3); }
    private:
        F m_f;
        Allocator m_a;
    };

    typedef R result_type;
    template <typename F>
    SharedFunction (F f, A a = A ())
        : m_ptr (new (typename CallType <F>::Allocator (a).allocate (1))
            CallType <F> (BEAST_MOVE_CAST(F)(f), a))
        { }
    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
        { }
    SharedFunction ()
        { }
    bool empty () const
        { return m_ptr == nullptr; }
    R operator() (P1 const& p1, P2 const& p2, P3 const& p3) const
        { return (*m_ptr)(p1, p2, p3); }

private:
    SharedPtr <Call> m_ptr;
};

//------------------------------------------------------------------------------

template <class R, class P1, class P2, class P3, class P4, class A>
class SharedFunction <R (P1, P2, P3, P4), A>
{
public:
    struct Call : public SharedObject
        { virtual R operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4) = 0; };

    template <typename F>
    struct CallType : Call
    {
        typedef typename A:: template rebind <CallType <F> >::other Allocator;
        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f)), m_a (a)
            { }
        R operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4)
            { return (m_f)(p1, p2, p3, p4); }
    private:
        F m_f;
        Allocator m_a;
    };

    typedef R result_type;
    template <typename F>
    SharedFunction (F f, A a = A ())
        : m_ptr (new (typename CallType <F>::Allocator (a).allocate (1))
            CallType <F> (BEAST_MOVE_CAST(F)(f), a))
        { }
    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
        { }
    SharedFunction ()
        { }
    bool empty () const
        { return m_ptr == nullptr; }
    R operator() (P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4) const
        { return (*m_ptr)(p1, p2, p3, p4); }

private:
    SharedPtr <Call> m_ptr;
};

//------------------------------------------------------------------------------

template <class R, class P1, class P2, class P3, class P4, class P5, class A>
class SharedFunction <R (P1, P2, P3, P4, P5), A>
{
public:
    struct Call : public SharedObject
        { virtual R operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                                P4 const& p4, P5 const& p5) = 0; };

    template <typename F>
    struct CallType : Call
    {
        typedef typename A:: template rebind <CallType <F> >::other Allocator;
        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f)), m_a (a)
            { }
        R operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                      P4 const& p4, P5 const& p5)
            { return (m_f)(p1, p2, p3, p4, p5); }
    private:
        F m_f;
        Allocator m_a;
    };

    typedef R result_type;
    template <typename F>
    SharedFunction (F f, A a = A ())
        : m_ptr (new (typename CallType <F>::Allocator (a).allocate (1))
            CallType <F> (BEAST_MOVE_CAST(F)(f), a))
        { }
    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
        { }
    SharedFunction ()
        { }
    bool empty () const
        { return m_ptr == nullptr; }
    R operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                  P4 const& p4, P5 const& p5) const
        { return (*m_ptr)(p1, p2, p3, p4, p5); }

private:
    SharedPtr <Call> m_ptr;
};

//------------------------------------------------------------------------------

template <class R, class P1, class P2, class P3, class P4, class P5, class P6, class A>
class SharedFunction <R (P1, P2, P3, P4, P5, P6), A>
{
public:
    struct Call : public SharedObject
        { virtual R operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                                P4 const& p4, P5 const& p5, P6 const& p6) = 0; };

    template <typename F>
    struct CallType : Call
    {
        typedef typename A:: template rebind <CallType <F> >::other Allocator;
        CallType (BEAST_MOVE_ARG(F) f, A a = A ())
            : m_f (BEAST_MOVE_CAST(F)(f)), m_a (a)
            { }
        R operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                      P4 const& p4, P5 const& p5, P6 const& p6)
            { return (m_f)(p1, p2, p3, p4, p5, p6); }
    private:
        F m_f;
        Allocator m_a;
    };

    typedef R result_type;
    template <typename F>
    SharedFunction (F f, A a = A ())
        : m_ptr (new (typename CallType <F>::Allocator (a).allocate (1))
            CallType <F> (BEAST_MOVE_CAST(F)(f), a))
        { }
    SharedFunction (SharedFunction const& other, A)
        : m_ptr (other.m_ptr)
        { }
    SharedFunction ()
        { }
    bool empty () const
        { return m_ptr == nullptr; }
    R operator() (P1 const& p1, P2 const& p2, P3 const& p3,
                  P4 const& p4, P5 const& p5, P6 const& p6) const
        { return (*m_ptr)(p1, p2, p3, p4, p5, p6); }

private:
    SharedPtr <Call> m_ptr;
};

#endif
