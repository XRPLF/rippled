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

#ifndef BEAST_FUNCTION_BEASTHEADER
#define BEAST_FUNCTION_BEASTHEADER

//
// Strong replacement for boost::function:
//
// #1 Bounded memory requirement, avoids the free store.
//
// #2 Always refers to a functor (i.e. is never invalid)
//
// #3 Default value (None) is a function that
//    returns a default object (the result type
//    constructed with a default constructor).
//

template <typename Signature, int Bytes = 128>
class Function;

//
// nullary function
//

template <typename R, int Bytes>
class Function <R (void), Bytes>
{
public:
    typedef R result_type;
    typedef Function self_type;

    struct None
    {
        typedef R result_type;
        result_type operator () () const
        {
            return result_type ();
        }
    };

    Function ()
    {
        constructCopyOf (None ());
    }

    Function (Function const& f)
    {
        f.getCall ().constructCopyInto (m_storage);
    }

    template <class Functor>
    Function (Functor const& f)
    {
        constructCopyOf (f);
    }

    ~Function ()
    {
        getCall ().~Call ();
    }

    Function& operator= (Function const& f)
    {
        getCall ().~Call ();
        f.getCall ().constructCopyInto (m_storage);
        return *this;
    }

    template <class Functor>
    Function& operator= (Functor const& f)
    {
        getCall ().~Call ();
        constructCopyOf (f);
        return *this;
    }

    result_type operator () ()
    {
        return getCall ().operator () ();
    }

private:
    template <class Functor>
    void constructCopyOf (Functor const& f)
    {
        // If this generates a compile error it means that
        // the functor is too large for the static buffer.
        // Increase the storage template parameter until
        // the error message goes away. This might cause
        // changes throughout the application with other
        // template classes that depend on the size.
        static_bassert (sizeof (StoredCall <Functor>) <= Bytes);
        new (m_storage) StoredCall <Functor> (f);
    }

private:
    struct Call
    {
        virtual ~Call () {}
        virtual void constructCopyInto (void* p) const = 0;
        virtual result_type operator () () = 0;
    };

    template <class Functor>
    struct StoredCall : Call
    {
        explicit StoredCall (Functor const& f) : m_f (f) { }
        StoredCall (const StoredCall& c) : m_f (c.m_f) { }
        void constructCopyInto (void* p) const
        {
            new (p) StoredCall (m_f);
        }
        result_type operator () ()
        {
            return m_f ();
        }
    private:
        Functor m_f;
    };

    Call& getCall ()
    {
        return *reinterpret_cast <Call*> (&m_storage[0]);
    }

    Call const& getCall () const
    {
        return *reinterpret_cast <Call const*> (&m_storage[0]);
    }

    char m_storage [Bytes]; // should be enough
};

//------------------------------------------------------------------------------

//
// unary function
//

template <typename R, typename T1, int Bytes>
class Function <R (T1 t1), Bytes>
{
public:
    typedef R result_type;
    typedef Function self_type;

    struct None
    {
        typedef R result_type;
        result_type operator () (T1) const
        {
            return result_type ();
        }
    };

    Function ()
    {
        constructCopyOf (None ());
    }

    Function (const Function& f)
    {
        f.getCall ().constructCopyInto (m_storage);
    }

    template <class Functor>
    Function (Functor const& f)
    {
        constructCopyOf (f);
    }

    ~Function ()
    {
        getCall ().~Call ();
    }

    Function& operator= (const Function& f)
    {
        getCall ().~Call ();
        f.getCall ().constructCopyInto (m_storage);
        return *this;
    }

    template <class Functor>
    Function& operator= (Functor const& f)
    {
        getCall ().~Call ();
        constructCopyOf (f);
        return *this;
    }

    result_type operator () (T1 t1)
    {
        return getCall ().operator () (t1);
    }

private:
    template <class Functor>
    void constructCopyOf (Functor const& f)
    {
        // If this generates a compile error it means that
        // the functor is too large for the static buffer.
        // Increase the storage template parameter until
        // the error message goes away. This might cause
        // changes throughout the application with other
        // template classes that depend on the size.
        static_bassert (sizeof (StoredCall <Functor>) <= Bytes);
        new (m_storage) StoredCall <Functor> (f);
    }

private:
    struct Call
    {
        virtual ~Call () {}
        virtual void constructCopyInto (void* p) const = 0;
        virtual result_type operator () (T1 t1) = 0;
    };

    template <class Functor>
    struct StoredCall : Call
    {
        explicit StoredCall (Functor const& f) : m_f (f) { }
        StoredCall (const StoredCall& c) : m_f (c.m_f) { }
        void constructCopyInto (void* p) const
        {
            new (p) StoredCall (m_f);
        }
        result_type operator () (T1 t1)
        {
            return m_f (t1);
        }
    private:
        Functor m_f;
    };

    Call& getCall ()
    {
        return *reinterpret_cast <Call*> (&m_storage[0]);
    }

    Call const& getCall () const
    {
        return *reinterpret_cast <Call const*> (&m_storage[0]);
    }

    char m_storage [Bytes]; // should be enough
};

#endif
