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

#ifndef BEAST_THREADS_DETAIL_BINDHANDLER_H_INCLUDED
#define BEAST_THREADS_DETAIL_BINDHANDLER_H_INCLUDED

namespace beast {
namespace detail {

/** Overloaded function that re-binds arguments to a handler. */
/** @{ */
template <class Handler, class P1>
class BindHandler1
{
private:
    Handler handler;
    P1 p1;

public:
    BindHandler1 (Handler const& handler_, P1 const& p1_)
        : handler (handler_)
        , p1 (p1_)
        { }

    BindHandler1 (Handler& handler_, P1 const& p1_)
        : handler (BEAST_MOVE_CAST(Handler)(handler_))
        , p1 (p1_)
        { }

    void operator()()
    {
        handler (
            static_cast <P1 const&> (p1)
            );
    }
    
    void operator()() const
    {
        handler (p1);
    }
};

template <class Handler, class P1>
BindHandler1 <Handler, P1> bindHandler (Handler handler, P1 const& p1)
{
    return BindHandler1 <Handler, P1> (handler, p1);
}

//------------------------------------------------------------------------------

template <class Handler, class P1, class P2>
class BindHandler2
{
private:
    Handler handler;
    P1 p1; P2 p2;

public:
    BindHandler2 (Handler const& handler_,
        P1 const& p1_, P2 const& p2_)
        : handler (handler_)
        , p1 (p1_), p2 (p2_)
        { }

    BindHandler2 (Handler& handler_,
        P1 const& p1_, P2 const& p2_)
        : handler (BEAST_MOVE_CAST(Handler)(handler_))
        , p1 (p1_), p2 (p2_)
        { }

    void operator()()
    {
        handler (
            static_cast <P1 const&> (p1), static_cast <P2 const&> (p2));
    }

    void operator()() const
        { handler (p1, p2); }
};

template <class Handler, class P1, class P2>
BindHandler2 <Handler, P1, P2> bindHandler (Handler handler,
    P1 const& p1, P2 const& p2)
{
    return BindHandler2 <Handler, P1, P2> (
        handler, p1, p2);
}

//------------------------------------------------------------------------------

template <class Handler, class P1, class P2, class P3>
class BindHandler3
{
private:
    Handler handler;
    P1 p1; P2 p2; P3 p3;

public:
    BindHandler3 (Handler const& handler_,
        P1 const& p1_, P2 const& p2_, P3 const& p3_)
        : handler (handler_)
        , p1 (p1_), p2 (p2_), p3 (p3_)
        { }

    BindHandler3 (Handler& handler_,
        P1 const& p1_, P2 const& p2_, P3 const& p3_)
        : handler (BEAST_MOVE_CAST(Handler)(handler_))
        , p1 (p1_), p2 (p2_), p3 (p3_)
        { }

    void operator()()
    {
        handler (
            static_cast <P1 const&> (p1), static_cast <P2 const&> (p2), static_cast <P3 const&> (p3));
    }

    void operator()() const
        { handler (p1, p2, p3); }
};

template <class Handler, class P1, class P2, class P3>
BindHandler3 <Handler, P1, P2, P3> bindHandler (Handler handler,
    P1 const& p1, P2 const& p2, P3 const& p3)
{
    return BindHandler3 <Handler, P1, P2, P3> (
        handler, p1, p2, p3);
}

//------------------------------------------------------------------------------

template <class Handler, class P1, class P2, class P3, class P4>
class BindHandler4
{
private:
    Handler handler;
    P1 p1; P2 p2; P3 p3; P4 p4;

public:
    BindHandler4 (Handler const& handler_,
        P1 const& p1_, P2 const& p2_, P3 const& p3_, P4 const& p4_)
        : handler (handler_)
        , p1 (p1_), p2 (p2_), p3 (p3_), p4 (p4_)
        { }

    BindHandler4 (Handler& handler_,
        P1 const& p1_, P2 const& p2_, P3 const& p3_, P4 const& p4_)
        : handler (BEAST_MOVE_CAST(Handler)(handler_))
        , p1 (p1_), p2 (p2_), p3 (p3_), p4 (p4_)
        { }

    void operator()()
    {
        handler (
            static_cast <P1 const&> (p1), static_cast <P2 const&> (p2), static_cast <P3 const&> (p3), 
            static_cast <P4 const&> (p4)
            );
    }

    void operator()() const
        { handler (p1, p2, p3, p4); }
};

template <class Handler, class P1, class P2, class P3, class P4>
BindHandler4 <Handler, P1, P2, P3, P4> bindHandler (Handler handler,
    P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4)
{
    return BindHandler4 <Handler, P1, P2, P3, P4> (
        handler, p1, p2, p3, p4);
}

//------------------------------------------------------------------------------

template <class Handler, class P1, class P2, class P3, class P4, class P5>
class BindHandler5
{
private:
    Handler handler;
    P1 p1; P2 p2; P3 p3; P4 p4; P5 p5;

public:
    BindHandler5 (Handler const& handler_,
        P1 const& p1_, P2 const& p2_, P3 const& p3_, P4 const& p4_, P5 const& p5_)
        : handler (handler_)
        , p1 (p1_), p2 (p2_), p3 (p3_), p4 (p4_), p5 (p5_)
        { }

    BindHandler5 (Handler& handler_,
        P1 const& p1_, P2 const& p2_, P3 const& p3_, P4 const& p4_, P5 const& p5_)
        : handler (BEAST_MOVE_CAST(Handler)(handler_))
        , p1 (p1_), p2 (p2_), p3 (p3_), p4 (p4_), p5 (p5_)
        { }

    void operator()()
    {
        handler (
            static_cast <P1 const&> (p1), static_cast <P2 const&> (p2), static_cast <P3 const&> (p3), 
            static_cast <P4 const&> (p4), static_cast <P5 const&> (p5)
            );
    }

    void operator()() const
        { handler (p1, p2, p3, p4, p5); }
};

template <class Handler, class P1, class P2, class P3, class P4, class P5>
BindHandler5 <Handler, P1, P2, P3, P4, P5> bindHandler (Handler handler,
    P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4, P5 const& p5)
{
    return BindHandler5 <Handler, P1, P2, P3, P4, P5> (
        handler, p1, p2, p3, p4, p5);
}

//------------------------------------------------------------------------------

template <class Handler, class P1, class P2, class P3, class P4, class P5, class P6>
class BindHandler6
{
private:
    Handler handler;
    P1 p1; P2 p2; P3 p3; P4 p4; P5 p5; P6 p6;

public:
    BindHandler6 (Handler const& handler_,
        P1 const& p1_, P2 const& p2_, P3 const& p3_, P4 const& p4_, P5 const& p5_, P6 const& p6_)
        : handler (handler_)
        , p1 (p1_), p2 (p2_), p3 (p3_), p4 (p4_), p5 (p5_), p6 (p6_)
        { }

    BindHandler6 (Handler& handler_,
        P1 const& p1_, P2 const& p2_, P3 const& p3_, P4 const& p4_, P5 const& p5_, P6 const& p6_)
        : handler (BEAST_MOVE_CAST(Handler)(handler_))
        , p1 (p1_), p2 (p2_), p3 (p3_), p4 (p4_), p5 (p5_), p6 (p6_)
        { }

    void operator()()
    {
        handler (
            static_cast <P1 const&> (p1), static_cast <P2 const&> (p2), static_cast <P3 const&> (p3), 
            static_cast <P4 const&> (p4), static_cast <P5 const&> (p5), static_cast <P6 const&> (p6)
            );
    }

    void operator()() const
        { handler (p1, p2, p3, p4, p5, p6); }
};

template <class Handler, class P1, class P2, class P3, class P4, class P5, class P6>
BindHandler6 <Handler, P1, P2, P3, P4, P5, P6> bindHandler (Handler handler,
    P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4, P5 const& p5, P6 const& p6)
{
    return BindHandler6 <Handler, P1, P2, P3, P4, P5, P6> (
        handler, p1, p2, p3, p4, p5, p6);
}

/** @} */

}
}

#endif
