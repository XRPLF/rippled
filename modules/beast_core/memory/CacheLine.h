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

#ifndef BEAST_CACHELINE_H_INCLUDED
#define BEAST_CACHELINE_H_INCLUDED

// Allows turning off of all padding,
// e.g. for memory-constrained systems or testing.
//
#define GLOBAL_PADDING_ENABLED 1

namespace beast
{

namespace CacheLine
{

#if GLOBAL_PADDING_ENABLED

/** Pad an object to start on a cache line boundary.
    Up to 8 constructor parameters are passed through.
*/
template <typename T>
class Aligned
{
public:
    Aligned ()
    {
        new (&get()) T;
    }

    template <class T1>
    Aligned (T1 t1)
    {
        new (&get()) T (t1);
    }

    template <class T1, class T2>
    Aligned (T1 t1, T2 t2)
    {
        new (&get()) T (t1, t2);
    }

    template <class T1, class T2, class T3>
    Aligned (T1 t1, T2 t2, T3 t3)
    {
        new (&get()) T (t1, t2, t3);
    }

    template <class T1, class T2, class T3, class T4>
    Aligned (T1 t1, T2 t2, T3 t3, T4 t4)
    {
        new (&get()) T (t1, t2, t3, t4);
    }

    template <class T1, class T2, class T3, class T4, class T5>
    Aligned (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
    {
        new (&get()) T (t1, t2, t3, t4, t5);
    }

    template <class T1, class T2, class T3, class T4, class T5, class T6>
    Aligned (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6)
    {
        new (&get()) T (t1, t2, t3, t4, t5, t6);
    }

    template <class T1, class T2, class T3, class T4, class T5, class T6, class T7>
    Aligned (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7)
    {
        new (&get()) T (t1, t2, t3, t4, t5, t6, t7);
    }

    template <class T1, class T2, class T3, class T4, class T5, class T6, class T7, class T8>
    Aligned (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7, T8 t8)
    {
        new (&get()) T (t1, t2, t3, t4, t5, t6, t7, t8);
    }

    ~Aligned ()
    {
        get().~T ();
    }

    T& operator= (T const& other)
    {
        return (get() = other);
    }

    T& get () noexcept { return *ptr(); }
    T& operator*  () noexcept { return  get(); }
    T* operator-> () noexcept { return &get(); }
    operator T&   () noexcept { return  get(); }
    operator T*   () noexcept { return &get(); }
    T const& get () const noexcept { return *ptr(); }
    const T& operator*  () const noexcept { return  get(); }
    const T* operator-> () const noexcept { return &get(); }
    operator T const&   () const noexcept { return  get(); }
    operator T const*   () const noexcept { return &get(); }

private:
    T* ptr () const noexcept
    {
        return (T*) ((uintptr_t (m_storage) + Memory::cacheLineAlignMask)
            & ~Memory::cacheLineAlignMask);
        /*
        return reinterpret_cast <T*> (Memory::pointerAdjustedForAlignment (
                                      m_storage, Memory::cacheLineBytes));
        */
    }

    char m_storage [ (sizeof (T) + Memory::cacheLineAlignMask) & ~Memory::cacheLineAlignMask];
};

//------------------------------------------------------------------------------

/** End-pads an object to completely fill straddling CPU cache lines.
    The caller must ensure that this object starts at the beginning
    of a cache line.
*/
template <typename T>
class Padded
{
public:
    Padded ()
    {
    }

    template <class T1>
    explicit Padded (T1 t1)
        : m_t (t1)
    {
    }

    template <class T1, class T2>
    Padded (T1 t1, T2 t2)
        : m_t (t1, t2)
    {
    }

    template <class T1, class T2, class T3>
    Padded (T1 t1, T2 t2, T3 t3)
        : m_t (t1, t2, t3)
    {
    }

    template <class T1, class T2, class T3, class T4>
    Padded (T1 t1, T2 t2, T3 t3, T4 t4)
        : m_t (t1, t2, t3, t4)
    {
    }

    template <class T1, class T2, class T3, class T4, class T5>
    Padded (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
        : m_t (t1, t2, t3, t4, t5)
    {
    }

    template <class T1, class T2, class T3, class T4, class T5, class T6>
    Padded (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6)
        : m_t (t1, t2, t3, t4, t5, t6)
    {
    }

    template <class T1, class T2, class T3, class T4, class T5, class T6, class T7>
    Padded (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7)
        : m_t (t1, t2, t3, t4, t5, t6, t7)
    {
    }

    template <class T1, class T2, class T3, class T4, class T5, class T6, class T7, class T8>
    Padded (T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7, T8 t8)
        : m_t (t1, t2, t3, t4, t5, t6, t7, t8)
    {
    }

    T& operator= (T const& other)
    {
        m_t = other;
        return m_t;
    }

    T& get() noexcept { return m_t;}
    T& operator*  () noexcept { return  get(); }
    T* operator-> () noexcept { return &get(); }
    operator T&   () noexcept { return  get(); }
    operator T*   () noexcept { return &get(); }
    T const& get () const noexcept { return m_t; }
    const T& operator*  () const noexcept { return  get(); }
    const T* operator-> () const noexcept { return &get(); }
    operator T const&   () const noexcept { return  get(); }
    operator T const*   () const noexcept { return &get(); }

private:
    T m_t;
    char pad [Memory::cacheLineAlignBytes - sizeof (T)];
};

#else

template <typename T>
class Aligned
{
public:
    Aligned ()
    { }

    template <class T1>
    explicit Aligned (const T1& t1)
        : m_t (t1) { }

    template <class T1, class T2>
    Aligned  (const T1& t1, const T2& t2)
        : m_t (t1, t2) { }

    template <class T1, class T2, class T3>
    Aligned  (const T1& t1, const T2& t2, const T3& t3)
        : m_t (t1, t2, t3) { }

    template <class T1, class T2, class T3, class T4>
    Aligned  (const T1& t1, const T2& t2, const T3& t3, const T4& t4)
        : m_t (t1, t2, t3, t4) { }

    template <class T1, class T2, class T3, class T4, class T5>
    Aligned  (const T1& t1, const T2& t2, const T3& t3,
              const T4& t4, const T5& t5)
        : m_t (t1, t2, t3, t4, t5) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6>
    Aligned  (const T1& t1, const T2& t2, const T3& t3,
              const T4& t4, const T5& t5, const T6& t6)
        : m_t (t1, t2, t3, t4, t5, t6) { }

    template < class T1, class T2, class T3, class T4,
             class T5, class T6, class T7 >
    Aligned  (const T1& t1, const T2& t2, const T3& t3, const T4& t4,
              const T5& t5, const T6& t6, const T7& t7)
        : m_t (t1, t2, t3, t4, t5, t6, t7) { }

    template < class T1, class T2, class T3, class T4,
             class T5, class T6, class T7, class T8 >
    Aligned  (const T1& t1, const T2& t2, const T3& t3, const T4& t4,
              const T5& t5, const T6& t6, const T7& t7, const T8& t8)
        : m_t (t1, t2, t3, t4, t5, t6, t7, t8) { }

    T& operator= (const T& other)
    {
        reutrn (m_t = other);
    }

    T& get () noexcept { return m_t; }
    T& operator*  () noexcept { return  get(); }
    T* operator-> () noexcept { return &get(); }
    operator T&   () noexcept { return  get(); }
    operator T*   () noexcept { return &get(); }
    T const& get () const noexcept { return m_t; }
    const T& operator*  () const noexcept { return  get(); }
    const T* operator-> () const noexcept { return &get(); }
    operator T const&   () const noexcept { return  get(); }
    operator T const*   () const noexcept { return &get(); }

private:
    T m_t;
};

template <typename T>
class Padded
{
public:
    Padded ()
    { }

    template <class T1>
    explicit Padded (const T1& t1)
        : m_t (t1) { }

    template <class T1, class T2>
    Padded   (const T1& t1, const T2& t2)
        : m_t (t1, t2) { }

    template <class T1, class T2, class T3>
    Padded   (const T1& t1, const T2& t2, const T3& t3)
        : m_t (t1, t2, t3) { }

    template <class T1, class T2, class T3, class T4>
    Padded   (const T1& t1, const T2& t2, const T3& t3, const T4& t4)
        : m_t (t1, t2, t3, t4) { }

    template <class T1, class T2, class T3, class T4, class T5>
    Padded   (const T1& t1, const T2& t2, const T3& t3,
              const T4& t4, const T5& t5)
        : m_t (t1, t2, t3, t4, t5) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6>
    Padded   (const T1& t1, const T2& t2, const T3& t3,
              const T4& t4, const T5& t5, const T6& t6)
        : m_t (t1, t2, t3, t4, t5, t6) { }

    template < class T1, class T2, class T3, class T4,
             class T5, class T6, class T7 >
    Padded   (const T1& t1, const T2& t2, const T3& t3, const T4& t4,
              const T5& t5, const T6& t6, const T7& t7)
        : m_t (t1, t2, t3, t4, t5, t6, t7) { }

    template < class T1, class T2, class T3, class T4,
             class T5, class T6, class T7, class T8 >
    Padded   (const T1& t1, const T2& t2, const T3& t3, const T4& t4,
              const T5& t5, const T6& t6, const T7& t7, const T8& t8)
        : m_t (t1, t2, t3, t4, t5, t6, t7, t8) { }

    void operator= (const T& other)
    {
        m_t = other;
    }

    T& get () noexcept { return m_t; }
    T& operator*  () noexcept { return  get(); }
    T* operator-> () noexcept { return &get(); }
    operator T&   () noexcept { return  get(); }
    operator T*   () noexcept { return &get(); }
    T const& get () const noexcept { return m_t; }
    const T& operator*  () const noexcept { return  get(); }
    const T* operator-> () const noexcept { return &get(); }
    operator T const&   () const noexcept { return  get(); }
    operator T const*   () const noexcept { return &get(); }

private:
    T m_t;
};

#endif

//
// Used to remove padding without changing code
//

template <typename T>
class Unpadded
{
public:
    Unpadded ()
    { }

    template <class T1>
    explicit Unpadded (const T1& t1)
        : m_t (t1) { }

    template <class T1, class T2>
    Unpadded (const T1& t1, const T2& t2)
        : m_t (t1, t2) { }

    template <class T1, class T2, class T3>
    Unpadded (const T1& t1, const T2& t2, const T3& t3)
        : m_t (t1, t2, t3) { }

    template <class T1, class T2, class T3, class T4>
    Unpadded (const T1& t1, const T2& t2, const T3& t3, const T4& t4)
        : m_t (t1, t2, t3, t4) { }

    template <class T1, class T2, class T3, class T4, class T5>
    Unpadded (const T1& t1, const T2& t2, const T3& t3,
              const T4& t4, const T5& t5)
        : m_t (t1, t2, t3, t4, t5) { }

    template <class T1, class T2, class T3, class T4, class T5, class T6>
    Unpadded (const T1& t1, const T2& t2, const T3& t3,
              const T4& t4, const T5& t5, const T6& t6)
        : m_t (t1, t2, t3, t4, t5, t6) { }

    template < class T1, class T2, class T3, class T4,
             class T5, class T6, class T7 >
    Unpadded (const T1& t1, const T2& t2, const T3& t3, const T4& t4,
              const T5& t5, const T6& t6, const T7& t7)
        : m_t (t1, t2, t3, t4, t5, t6, t7) { }

    template < class T1, class T2, class T3, class T4,
             class T5, class T6, class T7, class T8 >
    Unpadded (const T1& t1, const T2& t2, const T3& t3, const T4& t4,
              const T5& t5, const T6& t6, const T7& t7, const T8& t8)
        : m_t (t1, t2, t3, t4, t5, t6, t7, t8) { }

    T* operator= (const T& other)
    {
        return (m_t = other);
    }

    T& get () noexcept { return m_t; }
    T& operator*  () noexcept { return  get(); }
    T* operator-> () noexcept { return &get(); }
    operator T&   () noexcept { return  get(); }
    operator T*   () noexcept { return &get(); }
    T const& get () const noexcept { return m_t; }
    const T& operator*  () const noexcept { return  get(); }
    const T* operator-> () const noexcept { return &get(); }
    operator T const&   () const noexcept { return  get(); }
    operator T const*   () const noexcept { return &get(); }

private:
    T m_t;
};

}  // namespace CacheLine

}  // namespace beast

#endif
