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

#ifndef BEAST_CORE_H_INCLUDED
#define BEAST_CORE_H_INCLUDED

//------------------------------------------------------------------------------

/*  If you fail to make sure that all your compile units are building Beast with
    the same set of option flags, then there's a risk that different compile
    units will treat the classes as having different memory layouts, leading to
    very nasty memory corruption errors when they all get linked together.
    That's why it's best to always include the BeastConfig.h file before any
    beast headers.
*/
#ifndef BEAST_BEASTCONFIG_H_INCLUDED
# ifdef _MSC_VER
#  pragma message ("Have you included your BeastConfig.h file before including the Beast headers?")
# else
#  warning "Have you included your BeastConfig.h file before including the Beast headers?"
# endif
#endif

//------------------------------------------------------------------------------

#include "system/beast_TargetPlatform.h"

//
// Apply sensible defaults for the configuration settings
//

#ifndef BEAST_LOG_ASSERTIONS
# if BEAST_ANDROID
#  define BEAST_LOG_ASSERTIONS 1
# else
#  define BEAST_LOG_ASSERTIONS 0
# endif
#endif

#if BEAST_DEBUG && ! defined (BEAST_CHECK_MEMORY_LEAKS)
#define BEAST_CHECK_MEMORY_LEAKS 1
#endif

#ifndef BEAST_INCLUDE_ZLIB_CODE
#define BEAST_INCLUDE_ZLIB_CODE 1
#endif

#ifndef BEAST_ZLIB_INCLUDE_PATH
#define BEAST_ZLIB_INCLUDE_PATH <zlib.h>
#endif

/*  Config: BEAST_CATCH_UNHANDLED_EXCEPTIONS
    If enabled, this will add some exception-catching code to forward unhandled exceptions
    to your BEASTApplication::unhandledException() callback.
*/
#ifndef BEAST_CATCH_UNHANDLED_EXCEPTIONS
//#define BEAST_CATCH_UNHANDLED_EXCEPTIONS 1
#endif

#ifndef BEAST_BOOST_IS_AVAILABLE
#define BEAST_BOOST_IS_AVAILABLE 0
#endif

//------------------------------------------------------------------------------
//
// This is a hack to fix boost's goofy placeholders
//

#if BEAST_BOOST_IS_AVAILABLE
#ifdef BOOST_BIND_PLACEHOLDERS_HPP_INCLUDED
#error <boost/bind.hpp> must not be included before this file
#endif
// Prevent <boost/bind/placeholders.hpp> from being included
#define BOOST_BIND_PLACEHOLDERS_HPP_INCLUDED
#include <boost/bind/arg.hpp>
#include <boost/config.hpp>
// This based on <boost/bind/placeholders.cpp>
namespace boost {
namespace placeholders {
extern boost::arg<1> _1;
extern boost::arg<2> _2;
extern boost::arg<3> _3;
extern boost::arg<4> _4;
extern boost::arg<5> _5;
extern boost::arg<6> _6;
extern boost::arg<7> _7;
extern boost::arg<8> _8;
extern boost::arg<9> _9;
}
using namespace placeholders;
}
#endif

//------------------------------------------------------------------------------
//
// Choose a source of bind, placeholders, and function
//

#if !BEAST_BIND_USES_STD && !BEAST_BIND_USES_TR1 && !BEAST_BIND_USES_BOOST
# if BEAST_MSVC
#  define BEAST_BIND_USES_STD 1
# elif BEAST_IOS || BEAST_MAC
#  include <ciso646>                        // detect version of std::lib
#  if BEAST_IOS && BEAST_BOOST_IS_AVAILABLE // Work-around for iOS bugs with bind.
#   define BEAST_BIND_USES_BOOST 1
#  elif _LIBCPP_VERSION // libc++
#   define BEAST_BIND_USES_STD 1
#  else // libstdc++ (GNU)
#   define BEAST_BIND_USES_TR1 1
#  endif
# elif BEAST_LINUX || BEAST_BSD
#  define BEAST_BIND_USES_TR1 1
# else
#  define BEAST_BIND_USES_STD 1
# endif
#endif

#if BEAST_BIND_USES_STD
# include <functional>
#elif BEAST_BIND_USES_TR1
# include <tr1/functional>
#elif BEAST_BIND_USES_BOOST
# include <boost/bind.hpp>
# include <boost/function.hpp>
#endif

//------------------------------------------------------------------------------

#include "system/beast_StandardHeader.h"

#if BEAST_MSVC
# pragma warning (disable: 4251) // (DLL build warning, must be disabled before pushing the warning state)
# pragma warning (push)
# pragma warning (disable: 4786) // (long class name warning)
# ifdef __INTEL_COMPILER
#  pragma warning (disable: 1125)
# endif
#endif

// If the MSVC debug heap headers were included, disable
// the macros during the juce include since they conflict.
#ifdef _CRTDBG_MAP_ALLOC
#pragma push_macro("calloc")
#pragma push_macro("free")
#pragma push_macro("malloc")
#pragma push_macro("realloc")
#pragma push_macro("_recalloc")
#pragma push_macro("_aligned_free")
#pragma push_macro("_aligned_malloc")
#pragma push_macro("_aligned_offset_malloc")
#pragma push_macro("_aligned_realloc")
#pragma push_macro("_aligned_recalloc")
#pragma push_macro("_aligned_offset_realloc")
#pragma push_macro("_aligned_offset_recalloc")
#pragma push_macro("_aligned_msize")
#undef calloc
#undef free
#undef malloc
#undef realloc
#undef _recalloc
#undef _aligned_free
#undef _aligned_malloc
#undef _aligned_offset_malloc
#undef _aligned_realloc
#undef _aligned_recalloc
#undef _aligned_offset_realloc
#undef _aligned_offset_recalloc
#undef _aligned_msize
#endif

namespace beast
{

// Order matters, since headers don't have their own #include lines.
// Add new includes to the bottom.

#include "memory/beast_Uncopyable.h"

#include "system/beast_PlatformDefs.h"
#include "system/beast_TargetPlatform.h"
#include "system/beast_Functional.h"

#include "maths/beast_MathsFunctions.h"
#include "memory/beast_Atomic.h"
#include "memory/beast_AtomicCounter.h"
#include "memory/beast_AtomicFlag.h"
#include "memory/beast_AtomicPointer.h"
#include "memory/beast_AtomicState.h"
#include "containers/beast_LockFreeStack.h"
#include "threads/beast_SpinDelay.h"
#include "memory/beast_StaticObject.h"
#include "memory/beast_Memory.h"

#include "text/beast_String.h"

#include "threads/beast_CriticalSection.h"
#include "diagnostic/beast_FatalError.h"
#include "diagnostic/beast_SafeBool.h"
#include "diagnostic/beast_Error.h"
#include "diagnostic/beast_Debug.h"
#include "diagnostic/beast_Throw.h"

#include "text/beast_CharacterFunctions.h"
#include "text/beast_CharPointer_ASCII.h"
#include "text/beast_CharPointer_UTF16.h"
#include "text/beast_CharPointer_UTF32.h"
#include "text/beast_CharPointer_UTF8.h"
#include "text/beast_LexicalCast.h"

#include "time/beast_PerformedAtExit.h"
#include "diagnostic/beast_LeakChecked.h"
#include "memory/beast_ByteOrder.h"
#include "logging/beast_Logger.h"
#include "threads/beast_Thread.h"
#include "diagnostic/beast_FPUFlags.h"
#include "diagnostic/beast_ProtectedCall.h"
#include "containers/beast_AbstractFifo.h"
#include "containers/beast_Array.h"
#include "containers/beast_ArrayAllocationBase.h"
#include "containers/beast_DynamicObject.h"
#include "containers/beast_ElementComparator.h"
#include "maths/beast_Random.h"
#include "containers/beast_HashMap.h"
#include "containers/beast_List.h"
#include "containers/beast_LinkedListPointer.h"
#include "containers/beast_LockFreeQueue.h"
#include "containers/beast_NamedValueSet.h"
#include "containers/beast_OwnedArray.h"
#include "containers/beast_PropertySet.h"
#include "containers/beast_SharedObjectArray.h"
#include "containers/beast_ScopedValueSetter.h"
#include "containers/beast_SharedTable.h"
#include "containers/beast_SortedLookupTable.h"
#include "containers/beast_SortedSet.h"
#include "containers/beast_SparseSet.h"
#include "containers/beast_Variant.h"
#include "files/beast_DirectoryIterator.h"
#include "files/beast_File.h"
#include "files/beast_FileInputStream.h"
#include "files/beast_FileOutputStream.h"
#include "files/beast_FileSearchPath.h"
#include "files/beast_MemoryMappedFile.h"
#include "files/beast_RandomAccessFile.h"
#include "files/beast_TemporaryFile.h"
#include "json/beast_JSON.h"
#include "logging/beast_FileLogger.h"
#include "logging/beast_Logger.h"
#include "maths/beast_BigInteger.h"
#include "maths/beast_Expression.h"
#include "maths/beast_Interval.h"
#include "maths/beast_MathsFunctions.h"
#include "maths/beast_MurmurHash.h"
#include "maths/beast_Range.h"
#include "memory/beast_ByteOrder.h"
#include "memory/beast_HeapBlock.h"
#include "memory/beast_Memory.h"
#include "memory/beast_MemoryBlock.h"
#include "memory/beast_OptionalScopedPointer.h"
#include "memory/beast_SharedObject.h"
#include "memory/beast_ScopedPointer.h"
#include "threads/beast_SpinLock.h"
#include "memory/beast_SharedSingleton.h"
#include "memory/beast_WeakReference.h"
#include "memory/beast_MemoryAlignment.h"
#include "memory/beast_CacheLine.h"
#include "memory/beast_RecycledObjectPool.h"
#include "misc/beast_Main.h"
#include "misc/beast_Result.h"
#include "misc/beast_Uuid.h"
#include "misc/beast_WindowsRegistry.h"
#include "network/beast_IPAddress.h"
#include "network/beast_MACAddress.h"
#include "network/beast_NamedPipe.h"
#include "network/beast_Socket.h"
#include "network/beast_URL.h"
#include "streams/beast_BufferedInputStream.h"
#include "streams/beast_FileInputSource.h"
#include "streams/beast_InputSource.h"
#include "streams/beast_InputStream.h"
#include "streams/beast_MemoryInputStream.h"
#include "streams/beast_MemoryOutputStream.h"
#include "streams/beast_OutputStream.h"
#include "streams/beast_SubregionStream.h"

#include "system/beast_SystemStats.h"
#include "text/beast_Identifier.h"
#include "text/beast_LocalisedStrings.h"
#include "text/beast_NewLine.h"
#include "text/beast_StringArray.h"
#include "text/beast_StringPairArray.h"
#include "text/beast_StringPool.h"
#include "text/beast_TextDiff.h"
#include "threads/beast_ChildProcess.h"
#include "threads/beast_DynamicLibrary.h"
#include "threads/beast_HighResolutionTimer.h"
#include "threads/beast_InterProcessLock.h"
#include "threads/beast_Process.h"
#include "threads/beast_ReadWriteLock.h"
#include "threads/beast_ScopedLock.h"
#include "threads/beast_ScopedReadLock.h"
#include "threads/beast_ScopedWriteLock.h"
#include "threads/beast_ThreadLocalValue.h"
#include "threads/beast_ThreadPool.h"
#include "threads/beast_TimeSliceThread.h"
#include "threads/beast_WaitableEvent.h"
#include "time/beast_PerformanceCounter.h"
#include "time/beast_RelativeTime.h"
#include "time/beast_Time.h"
#include "diagnostic/beast_UnitTest.h"
#include "xml/beast_XmlDocument.h"
#include "xml/beast_XmlElement.h"
#include "diagnostic/beast_UnitTestUtilities.h"
#include "zip/beast_GZIPCompressorOutputStream.h"
#include "zip/beast_GZIPDecompressorInputStream.h"
#include "zip/beast_ZipFile.h"

}

#ifdef _CRTDBG_MAP_ALLOC
#pragma pop_macro("_aligned_msize")
#pragma pop_macro("_aligned_offset_recalloc")
#pragma pop_macro("_aligned_offset_realloc")
#pragma pop_macro("_aligned_recalloc")
#pragma pop_macro("_aligned_realloc")
#pragma pop_macro("_aligned_offset_malloc")
#pragma pop_macro("_aligned_malloc")
#pragma pop_macro("_aligned_free")
#pragma pop_macro("_recalloc")
#pragma pop_macro("realloc")
#pragma pop_macro("malloc")
#pragma pop_macro("free")
#pragma pop_macro("calloc")
#endif

#if BEAST_MSVC
#pragma warning (pop)
#endif

//------------------------------------------------------------------------------

#endif
