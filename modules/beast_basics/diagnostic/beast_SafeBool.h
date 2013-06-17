/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

#ifndef BEAST_SAFEBOOL_BEASTHEADER
#define BEAST_SAFEBOOL_BEASTHEADER

/**
  Safe evaluation of class as `bool`.

  This allows a class to be safely evaluated as a bool without the usual harmful
  side effects of the straightforward operator conversion approach. To use it,
  derive your class from SafeBool and implement `asBoolean()` as:

  @code

  bool asBoolean () const;

  @endcode

  Ideas from http://www.artima.com/cppsource/safebool.html

  @class SafeBool

  @ingroup beast_core
*/

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

template <typename T = void>
class SafeBool : public SafeBoolBase
{
public:
    operator boolean_t () const
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

#endif


