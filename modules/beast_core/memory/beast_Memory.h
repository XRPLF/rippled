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

#ifndef BEAST_MEMORY_BEASTHEADER
#define BEAST_MEMORY_BEASTHEADER

//==============================================================================
/** Fills a block of memory with zeros. */
inline void zeromem (void* memory, size_t numBytes) noexcept        { memset (memory, 0, numBytes); }

/** Overwrites a structure or object with zeros. */
template <typename Type>
inline void zerostruct (Type& structure) noexcept                   { memset (&structure, 0, sizeof (structure)); }

/** Delete an object pointer, and sets the pointer to null.

    Remember that it's not good c++ practice to use delete directly - always try to use a ScopedPointer
    or other automatic lifetime-management system rather than resorting to deleting raw pointers!
*/
template <typename Type>
inline void deleteAndZero (Type& pointer)                           { delete pointer; pointer = nullptr; }

/** A handy function which adds a number of bytes to any type of pointer and returns the result.
    This can be useful to avoid casting pointers to a char* and back when you want to move them by
    a specific number of bytes,
*/
template <typename Type, typename IntegerType>
inline Type* addBytesToPointer (Type* pointer, IntegerType bytes) noexcept  { return (Type*) (((char*) pointer) + bytes); }

/** A handy function which returns the difference between any two pointers, in bytes.
    The address of the second pointer is subtracted from the first, and the difference in bytes is returned.
*/
template <typename Type1, typename Type2>
inline int getAddressDifference (Type1* pointer1, Type2* pointer2) noexcept  { return (int) (((const char*) pointer1) - (const char*) pointer2); }

/** If a pointer is non-null, this returns a new copy of the object that it points to, or safely returns
    nullptr if the pointer is null.
*/
template <class Type>
inline Type* createCopyIfNotNull (const Type* pointer)     { return pointer != nullptr ? new Type (*pointer) : nullptr; }

//==============================================================================
#if BEAST_MAC || BEAST_IOS || DOXYGEN

 /** A handy C++ wrapper that creates and deletes an NSAutoreleasePool object using RAII.
     You should use the BEAST_AUTORELEASEPOOL macro to create a local auto-release pool on the stack.
 */
 class BEAST_API ScopedAutoReleasePool
 {
 public:
     ScopedAutoReleasePool();
     ~ScopedAutoReleasePool();

 private:
     void* pool;

     BEAST_DECLARE_NON_COPYABLE (ScopedAutoReleasePool)
 };

 /** A macro that can be used to easily declare a local ScopedAutoReleasePool
     object for RAII-based obj-C autoreleasing.
     Because this may use the \@autoreleasepool syntax, you must follow the macro with
     a set of braces to mark the scope of the pool.
 */
#if (BEAST_COMPILER_SUPPORTS_ARC && defined (__OBJC__)) || DOXYGEN
 #define BEAST_AUTORELEASEPOOL  @autoreleasepool
#else
 #define BEAST_AUTORELEASEPOOL  const beast::ScopedAutoReleasePool BEAST_JOIN_MACRO (autoReleasePool_, __LINE__);
#endif

#else
 #define BEAST_AUTORELEASEPOOL
#endif

//==============================================================================
/* In a Windows DLL build, we'll expose some malloc/free functions that live inside the DLL, and use these for
   allocating all the objects - that way all beast objects in the DLL and in the host will live in the same heap,
   avoiding problems when an object is created in one module and passed across to another where it is deleted.
   By piggy-backing on the BEAST_LEAK_DETECTOR macro, these allocators can be injected into most beast classes.
*/
#if BEAST_MSVC && (defined (BEAST_DLL) || defined (BEAST_DLL_BUILD)) && ! (BEAST_DISABLE_DLL_ALLOCATORS || DOXYGEN)
 extern BEAST_API void* beastDLL_malloc (size_t);
 extern BEAST_API void  beastDLL_free (void*);

 #define BEAST_LEAK_DETECTOR(OwnerClass)  public:\
    static void* operator new (size_t sz)           { return beast::beastDLL_malloc (sz); } \
    static void* operator new (size_t, void* p)     { return p; } \
    static void operator delete (void* p)           { beast::beastDLL_free (p); } \
    static void operator delete (void*, void*)      {}
#endif

//==============================================================================
/** (Deprecated) This was a Windows-specific way of checking for object leaks - now please
    use the BEAST_LEAK_DETECTOR instead.
*/
#ifndef beast_UseDebuggingNewOperator
 #define beast_UseDebuggingNewOperator
#endif


#endif   // BEAST_MEMORY_BEASTHEADER
