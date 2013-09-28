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

#ifndef BEAST_SAFEBOOL_H_INCLUDED
#define BEAST_SAFEBOOL_H_INCLUDED

namespace beast {

namespace detail {

class SafeBoolBase
{
private:
    void disallowed () const { }

public:
    void allowed () const { }

protected:
    typedef void (SafeBoolBase::*boolean_t) () const;

    SafeBoolBase () { }
    SafeBoolBase (SafeBoolBase const&) { }
    SafeBoolBase& operator= (SafeBoolBase const&)
    {
        return *this;
    }
    ~SafeBoolBase () { }
};

}

/** Safe evaluation of class as `bool`.

    This allows a class to be safely evaluated as a bool without the usual
    harmful side effects of the straightforward operator conversion approach.
    To use it, derive your class from SafeBool and implement `asBoolean()` as:

    @code

    bool asBoolean () const;

    @endcode

    Ideas from http://www.artima.com/cppsource/safebool.html

    @class SafeBool
*/
template <typename T = void>
class SafeBool : public detail::SafeBoolBase
{
public:
    operator detail::SafeBoolBase::boolean_t () const
    {
        return (static_cast <T const*> (this))->asBoolean ()
               ? &SafeBoolBase::allowed : 0;
    }

protected:
    ~SafeBool () { }
};

template <typename T, typename U>
void operator== (SafeBool <T> const& lhs, SafeBool <U> const& rhs)
{
    lhs.disallowed ();
}

template <typename T, typename U>
void operator!= (SafeBool <T> const& lhs, SafeBool <U> const& rhs)
{
    lhs.disallowed ();
}

}

#endif


