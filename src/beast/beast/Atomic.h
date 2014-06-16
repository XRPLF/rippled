//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_ATOMIC_H_INCLUDED
#define BEAST_ATOMIC_H_INCLUDED

#include <beast/Config.h>
#include <beast/StaticAssert.h>

#include <beast/utility/noexcept.h>

#include <cstdint>

namespace beast {

//==============================================================================
/**
    Simple class to hold a primitive value and perform atomic operations on it.

    The type used must be a 32 or 64 bit primitive, like an int, pointer, etc.
    There are methods to perform most of the basic atomic operations.
*/
template <typename Type>
class Atomic
{
public:
    /** Creates a new value, initialised to zero. */
    inline Atomic() noexcept
        : value (0)
    {
    }

    /** Creates a new value, with a given initial value. */
    inline Atomic (const Type initialValue) noexcept
        : value (initialValue)
    {
    }

    /** Copies another value (atomically). */
    inline Atomic (const Atomic& other) noexcept
        : value (other.get())
    {
    }

    /** Destructor. */
    inline ~Atomic() noexcept
    {
        // This class can only be used for types which are 32 or 64 bits in size.
        static_bassert (sizeof (Type) == 4 || sizeof (Type) == 8);
    }

    /** Atomically reads and returns the current value. */
    Type get() const noexcept;

    /** Copies another value onto this one (atomically). */
     Atomic& operator= (const Atomic& other) noexcept
      { exchange (other.get()); return *this; }

    /** Copies another value onto this one (atomically). */
     Atomic& operator= (const Type newValue) noexcept
      { exchange (newValue); return *this; }

    /** Atomically sets the current value. */
    void set (Type newValue) noexcept
      { exchange (newValue); }

    /** Atomically sets the current value, returning the value that was replaced. */
    Type exchange (Type value) noexcept;

    /** Atomically adds a number to this value, returning the new value. */
    Type operator+= (Type amountToAdd) noexcept;

    /** Atomically subtracts a number from this value, returning the new value. */
    Type operator-= (Type amountToSubtract) noexcept;

    /** Atomically increments this value, returning the new value. */
    Type operator++() noexcept;

    /** Atomically decrements this value, returning the new value. */
    Type operator--() noexcept;

    /** Atomically compares this value with a target value, and if it is equal, sets
        this to be equal to a new value.

        This operation is the atomic equivalent of doing this:
        @code
        bool compareAndSetBool (Type newValue, Type valueToCompare)
        {
            if (get() == valueToCompare)
            {
                set (newValue);
                return true;
            }

            return false;
        }
        @endcode

        @returns true if the comparison was true and the value was replaced; false if
                 the comparison failed and the value was left unchanged.
        @see compareAndSetValue
    */
    bool compareAndSetBool (Type newValue, Type valueToCompare) noexcept;

    /** Atomically compares this value with a target value, and if it is equal, sets
        this to be equal to a new value.

        This operation is the atomic equivalent of doing this:
        @code
        Type compareAndSetValue (Type newValue, Type valueToCompare)
        {
            Type oldValue = get();
            if (oldValue == valueToCompare)
                set (newValue);

            return oldValue;
        }
        @endcode

        @returns the old value before it was changed.
        @see compareAndSetBool
    */
    Type compareAndSetValue (Type newValue, Type valueToCompare) noexcept;

    //==============================================================================
   #if BEAST_64BIT
    BEAST_ALIGN (8)
   #else
    BEAST_ALIGN (4)
   #endif

    /** The raw value that this class operates on.
        This is exposed publically in case you need to manipulate it directly
        for performance reasons.
    */
    volatile Type value;

private:
    template <typename Dest, typename Source>
    static inline Dest castTo (Source value) noexcept { union { Dest d; Source s; } u; u.s = value; return u.d; }

    static inline Type castFrom32Bit (std::int32_t value) noexcept { return castTo <Type, std::int32_t> (value); }
    static inline Type castFrom64Bit (std::int64_t value) noexcept { return castTo <Type, std::int64_t> (value); }
    static inline std::int32_t castTo32Bit (Type value) noexcept { return castTo <std::int32_t, Type> (value); }
    static inline std::int64_t castTo64Bit (Type value) noexcept { return castTo <std::int64_t, Type> (value); }


    Type operator++ (int); // better to just use pre-increment with atomics..
    Type operator-- (int);

    /** This templated negate function will negate pointers as well as integers */
    template <typename ValueType>
    inline ValueType negateValue (ValueType n) noexcept
    {
        return sizeof (ValueType) == 1 ? (ValueType) -(signed char) n
            : (sizeof (ValueType) == 2 ? (ValueType) -(short) n
            : (sizeof (ValueType) == 4 ? (ValueType) -(int) n
            : ((ValueType) -(std::int64_t) n)));
    }

    /** This templated negate function will negate pointers as well as integers */
    template <typename PointerType>
    inline PointerType* negateValue (PointerType* n) noexcept
    {
        return reinterpret_cast <PointerType*> (-reinterpret_cast <std::intptr_t> (n));
    }
};


//==============================================================================
/*
    The following code is in the header so that the atomics can be inlined where possible...
*/
#if BEAST_IOS || (BEAST_MAC && (BEAST_PPC || BEAST_CLANG || __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 2)))
  #define BEAST_ATOMICS_MAC 1        // Older OSX builds using gcc4.1 or earlier

  #if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_5
    #define BEAST_MAC_ATOMICS_VOLATILE
  #else
    #define BEAST_MAC_ATOMICS_VOLATILE volatile
  #endif

  #if BEAST_PPC || BEAST_IOS
    // None of these atomics are available for PPC or for iOS 3.1 or earlier!!
    template <typename Type> static Type OSAtomicAdd64Barrier (Type b, BEAST_MAC_ATOMICS_VOLATILE Type* a) noexcept  { bassertfalse; return *a += b; }
    template <typename Type> static Type OSAtomicIncrement64Barrier (BEAST_MAC_ATOMICS_VOLATILE Type* a) noexcept    { bassertfalse; return ++*a; }
    template <typename Type> static Type OSAtomicDecrement64Barrier (BEAST_MAC_ATOMICS_VOLATILE Type* a) noexcept    { bassertfalse; return --*a; }
    template <typename Type> static bool OSAtomicCompareAndSwap64Barrier (Type old, Type newValue, BEAST_MAC_ATOMICS_VOLATILE Type* value) noexcept
        { bassertfalse; if (old == *value) { *value = newValue; return true; } return false; }
    #define BEAST_64BIT_ATOMICS_UNAVAILABLE 1
  #endif

#elif BEAST_CLANG && BEAST_LINUX
  #define BEAST_ATOMICS_GCC 1

//==============================================================================
#elif BEAST_GCC
  #define BEAST_ATOMICS_GCC 1        // GCC with intrinsics

  #if BEAST_IOS || BEAST_ANDROID // (64-bit ops will compile but not link on these mobile OSes)
    #define BEAST_64BIT_ATOMICS_UNAVAILABLE 1
  #endif

//==============================================================================
#else
  #define BEAST_ATOMICS_WINDOWS 1    // Windows with intrinsics

  #if BEAST_USE_INTRINSICS
    #ifndef __INTEL_COMPILER
     #pragma intrinsic (_InterlockedExchange, _InterlockedIncrement, _InterlockedDecrement, _InterlockedCompareExchange, \
                        _InterlockedCompareExchange64, _InterlockedExchangeAdd, _ReadWriteBarrier)
    #endif
    #define beast_InterlockedExchange(a, b)              _InterlockedExchange(a, b)
    #define beast_InterlockedIncrement(a)                _InterlockedIncrement(a)
    #define beast_InterlockedDecrement(a)                _InterlockedDecrement(a)
    #define beast_InterlockedExchangeAdd(a, b)           _InterlockedExchangeAdd(a, b)
    #define beast_InterlockedCompareExchange(a, b, c)    _InterlockedCompareExchange(a, b, c)
    #define beast_InterlockedCompareExchange64(a, b, c)  _InterlockedCompareExchange64(a, b, c)
    #define beast_MemoryBarrier _ReadWriteBarrier
  #else
    long beast_InterlockedExchange (volatile long* a, long b) noexcept;
    long beast_InterlockedIncrement (volatile long* a) noexcept;
    long beast_InterlockedDecrement (volatile long* a) noexcept;
    long beast_InterlockedExchangeAdd (volatile long* a, long b) noexcept;
    long beast_InterlockedCompareExchange (volatile long* a, long b, long c) noexcept;
    __int64 beast_InterlockedCompareExchange64 (volatile __int64* a, __int64 b, __int64 c) noexcept;
    inline void beast_MemoryBarrier() noexcept  { long x = 0; beast_InterlockedIncrement (&x); }
  #endif

  #if BEAST_64BIT
    #ifndef __INTEL_COMPILER
     #pragma intrinsic (_InterlockedExchangeAdd64, _InterlockedExchange64, _InterlockedIncrement64, _InterlockedDecrement64)
    #endif
    #define beast_InterlockedExchangeAdd64(a, b)     _InterlockedExchangeAdd64(a, b)
    #define beast_InterlockedExchange64(a, b)        _InterlockedExchange64(a, b)
    #define beast_InterlockedIncrement64(a)          _InterlockedIncrement64(a)
    #define beast_InterlockedDecrement64(a)          _InterlockedDecrement64(a)
  #else
    // None of these atomics are available in a 32-bit Windows build!!
    template <typename Type> static Type beast_InterlockedExchangeAdd64 (volatile Type* a, Type b) noexcept  { bassertfalse; Type old = *a; *a += b; return old; }
    template <typename Type> static Type beast_InterlockedExchange64 (volatile Type* a, Type b) noexcept     { bassertfalse; Type old = *a; *a = b; return old; }
    template <typename Type> static Type beast_InterlockedIncrement64 (volatile Type* a) noexcept            { bassertfalse; return ++*a; }
    template <typename Type> static Type beast_InterlockedDecrement64 (volatile Type* a) noexcept            { bassertfalse; return --*a; }
    #define BEAST_64BIT_ATOMICS_UNAVAILABLE 1
  #endif
#endif

#if BEAST_MSVC
  #pragma warning (push)
  #pragma warning (disable: 4311)  // (truncation warning)
#endif

//==============================================================================
template <typename Type>
inline Type Atomic<Type>::get() const noexcept
{
  #if BEAST_ATOMICS_MAC
    return sizeof (Type) == 4 ? castFrom32Bit ((std::int32_t) OSAtomicAdd32Barrier ((int32_t) 0, (BEAST_MAC_ATOMICS_VOLATILE int32_t*) &value))
                              : castFrom64Bit ((std::int64_t) OSAtomicAdd64Barrier ((int64_t) 0, (BEAST_MAC_ATOMICS_VOLATILE int64_t*) &value));
  #elif BEAST_ATOMICS_WINDOWS
    return sizeof (Type) == 4 ? castFrom32Bit ((std::int32_t) beast_InterlockedExchangeAdd ((volatile long*) &value, (long) 0))
                              : castFrom64Bit ((std::int64_t) beast_InterlockedExchangeAdd64 ((volatile __int64*) &value, (__int64) 0));
  #elif BEAST_ATOMICS_GCC
    return sizeof (Type) == 4 ? castFrom32Bit ((std::int32_t) __sync_add_and_fetch ((volatile std::int32_t*) &value, 0))
                              : castFrom64Bit ((std::int64_t) __sync_add_and_fetch ((volatile std::int64_t*) &value, 0));
  #endif
}

template <typename Type>
inline Type Atomic<Type>::exchange (const Type newValue) noexcept
{
  #if BEAST_ATOMICS_MAC || BEAST_ATOMICS_GCC
    Type currentVal = value;
    while (! compareAndSetBool (newValue, currentVal)) { currentVal = value; }
    return currentVal;
  #elif BEAST_ATOMICS_WINDOWS
    return sizeof (Type) == 4 ? castFrom32Bit ((std::int32_t) beast_InterlockedExchange ((volatile long*) &value, (long) castTo32Bit (newValue)))
                              : castFrom64Bit ((std::int64_t) beast_InterlockedExchange64 ((volatile __int64*) &value, (__int64) castTo64Bit (newValue)));
  #endif
}

template <typename Type>
inline Type Atomic<Type>::operator+= (const Type amountToAdd) noexcept
{
  #if BEAST_ATOMICS_MAC
#   ifdef __clang__
#       pragma clang diagnostic push
#       pragma clang diagnostic ignored "-Wint-to-void-pointer-cast"
#       pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#   endif
    return sizeof (Type) == 4 ? (Type) OSAtomicAdd32Barrier ((int32_t) castTo32Bit (amountToAdd), (BEAST_MAC_ATOMICS_VOLATILE int32_t*) &value)
                              : (Type) OSAtomicAdd64Barrier ((int64_t) amountToAdd, (BEAST_MAC_ATOMICS_VOLATILE int64_t*) &value);
#   ifdef __clang__
#       pragma clang diagnostic pop
#   endif
  #elif BEAST_ATOMICS_WINDOWS
    return sizeof (Type) == 4 ? (Type) (beast_InterlockedExchangeAdd ((volatile long*) &value, (long) amountToAdd) + (long) amountToAdd)
                              : (Type) (beast_InterlockedExchangeAdd64 ((volatile __int64*) &value, (__int64) amountToAdd) + (__int64) amountToAdd);
  #elif BEAST_ATOMICS_GCC
    return (Type) __sync_add_and_fetch (&value, amountToAdd);
  #endif
}

template <typename Type>
inline Type Atomic<Type>::operator-= (const Type amountToSubtract) noexcept
{
    return operator+= (negateValue (amountToSubtract));
}

template <typename Type>
inline Type Atomic<Type>::operator++() noexcept
{
  #if BEAST_ATOMICS_MAC
#   ifdef __clang__
#       pragma clang diagnostic push
#       pragma clang diagnostic ignored "-Wint-to-void-pointer-cast"
#       pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#   endif
    return sizeof (Type) == 4 ? (Type) OSAtomicIncrement32Barrier ((BEAST_MAC_ATOMICS_VOLATILE int32_t*) &value)
                              : (Type) OSAtomicIncrement64Barrier ((BEAST_MAC_ATOMICS_VOLATILE int64_t*) &value);
#   ifdef __clang__
#       pragma clang diagnostic pop
#   endif
  #elif BEAST_ATOMICS_WINDOWS
    return sizeof (Type) == 4 ? (Type) beast_InterlockedIncrement ((volatile long*) &value)
                              : (Type) beast_InterlockedIncrement64 ((volatile __int64*) &value);
  #elif BEAST_ATOMICS_GCC
    return (Type) __sync_add_and_fetch (&value, (Type) 1);
  #endif
}

template <typename Type>
inline Type Atomic<Type>::operator--() noexcept
{
  #if BEAST_ATOMICS_MAC
#   ifdef __clang__
#       pragma clang diagnostic push
#       pragma clang diagnostic ignored "-Wint-to-void-pointer-cast"
#       pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#   endif
    return sizeof (Type) == 4 ? (Type) OSAtomicDecrement32Barrier ((BEAST_MAC_ATOMICS_VOLATILE int32_t*) &value)
                              : (Type) OSAtomicDecrement64Barrier ((BEAST_MAC_ATOMICS_VOLATILE int64_t*) &value);
#   ifdef __clang__
#       pragma clang diagnostic pop
#   endif
  #elif BEAST_ATOMICS_WINDOWS
    return sizeof (Type) == 4 ? (Type) beast_InterlockedDecrement ((volatile long*) &value)
                              : (Type) beast_InterlockedDecrement64 ((volatile __int64*) &value);
  #elif BEAST_ATOMICS_GCC
    return (Type) __sync_add_and_fetch (&value, (Type) -1);
  #endif
}

template <typename Type>
inline bool Atomic<Type>::compareAndSetBool (const Type newValue, const Type valueToCompare) noexcept
{
  #if BEAST_ATOMICS_MAC
    return sizeof (Type) == 4 ? OSAtomicCompareAndSwap32Barrier ((int32_t) castTo32Bit (valueToCompare), (int32_t) castTo32Bit (newValue), (BEAST_MAC_ATOMICS_VOLATILE int32_t*) &value)
                              : OSAtomicCompareAndSwap64Barrier ((int64_t) castTo64Bit (valueToCompare), (int64_t) castTo64Bit (newValue), (BEAST_MAC_ATOMICS_VOLATILE int64_t*) &value);
  #elif BEAST_ATOMICS_WINDOWS
    return compareAndSetValue (newValue, valueToCompare) == valueToCompare;
  #elif BEAST_ATOMICS_GCC
    return sizeof (Type) == 4 ? __sync_bool_compare_and_swap ((volatile std::int32_t*) &value, castTo32Bit (valueToCompare), castTo32Bit (newValue))
                              : __sync_bool_compare_and_swap ((volatile std::int64_t*) &value, castTo64Bit (valueToCompare), castTo64Bit (newValue));
  #endif
}

template <typename Type>
inline Type Atomic<Type>::compareAndSetValue (const Type newValue, const Type valueToCompare) noexcept
{
  #if BEAST_ATOMICS_MAC
    for (;;) // Annoying workaround for only having a bool CAS operation..
    {
        if (compareAndSetBool (newValue, valueToCompare))
            return valueToCompare;

        const Type result = value;
        if (result != valueToCompare)
            return result;
    }

  #elif BEAST_ATOMICS_WINDOWS
    return sizeof (Type) == 4 ? castFrom32Bit ((std::int32_t) beast_InterlockedCompareExchange ((volatile long*) &value, (long) castTo32Bit (newValue), (long) castTo32Bit (valueToCompare)))
                              : castFrom64Bit ((std::int64_t) beast_InterlockedCompareExchange64 ((volatile __int64*) &value, (__int64) castTo64Bit (newValue), (__int64) castTo64Bit (valueToCompare)));
  #elif BEAST_ATOMICS_GCC
    return sizeof (Type) == 4 ? castFrom32Bit ((std::int32_t) __sync_val_compare_and_swap ((volatile std::int32_t*) &value, castTo32Bit (valueToCompare), castTo32Bit (newValue)))
                              : castFrom64Bit ((std::int64_t) __sync_val_compare_and_swap ((volatile std::int64_t*) &value, castTo64Bit (valueToCompare), castTo64Bit (newValue)));
  #endif
}

inline void memoryBarrier() noexcept
{
  #if BEAST_ATOMICS_MAC
    OSMemoryBarrier();
  #elif BEAST_ATOMICS_GCC
    __sync_synchronize();
  #elif BEAST_ATOMICS_WINDOWS
    beast_MemoryBarrier();
  #endif
}

#if BEAST_MSVC
  #pragma warning (pop)
#endif

}

#endif
