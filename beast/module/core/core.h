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

// TargetPlatform.h should not use anything from BeastConfig.h
#include <beast/Config.h>
#include <beast/config/ContractChecks.h>

#if BEAST_MSVC
# pragma warning (disable: 4251) // (DLL build warning, must be disabled before pushing the warning state)
# pragma warning (push)
# pragma warning (disable: 4786) // (long class name warning)
# ifdef __INTEL_COMPILER
#  pragma warning (disable: 1125)
# endif
#endif

//------------------------------------------------------------------------------

// New header-only library modeled more closely according to boost
#include <beast/SmartPtr.h>
#include <beast/StaticAssert.h>
#include <beast/Uncopyable.h>
#include <beast/Atomic.h>
#include <beast/Arithmetic.h>
#include <beast/ByteOrder.h>
#include <beast/HeapBlock.h>
#include <beast/Memory.h>
#include <beast/Intrusive.h>
#include <beast/Strings.h>
#include <beast/Threads.h>

#include <beast/utility/Debug.h>
#include <beast/utility/Error.h>
#include <beast/utility/Journal.h>
#include <beast/utility/LeakChecked.h>
#include <beast/utility/PropertyStream.h>
#include <beast/utility/StaticObject.h>

#include <beast/module/core/system/StandardIncludes.h>

namespace beast
{

class InputStream;
class OutputStream;
class FileInputStream;
class FileOutputStream;

} // beast

// Order matters, since headers don't have their own #include lines.
// Add new includes to the bottom.

#include <beast/module/core/time/AtExitHook.h>
#include <beast/module/core/time/Time.h>
#include <beast/module/core/threads/ScopedLock.h>
#include <beast/module/core/threads/CriticalSection.h>
#include <beast/module/core/containers/ElementComparator.h>

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
#include <beast/module/core/containers/ArrayAllocationBase.h>
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

#include <beast/module/core/containers/Array.h>

#include <beast/module/core/misc/Result.h>
#include <beast/module/core/text/StringArray.h>
#include <beast/module/core/memory/MemoryBlock.h>
#include <beast/module/core/files/File.h>

#include <beast/module/core/thread/MutexTraits.h>
#include <beast/module/core/diagnostic/FatalError.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/module/core/maths/Math.h>
#include <beast/module/core/logging/Logger.h>
#include <beast/module/core/containers/LinkedListPointer.h>
#include <beast/module/core/maths/Random.h>
#include <beast/module/core/text/StringPairArray.h>
#include <beast/module/core/containers/ScopedValueSetter.h>
#include <beast/module/core/maths/Range.h>
#include <beast/module/core/files/DirectoryIterator.h>
#include <beast/module/core/streams/InputStream.h>
#include <beast/module/core/files/FileInputStream.h>
#include <beast/module/core/streams/InputSource.h>
#include <beast/module/core/streams/FileInputSource.h>
#include <beast/module/core/streams/OutputStream.h>
#include <beast/module/core/files/FileOutputStream.h>
#include <beast/module/core/files/FileSearchPath.h>
#include <beast/module/core/files/RandomAccessFile.h>
#include <beast/module/core/files/TemporaryFile.h>
#include <beast/module/core/logging/Logger.h>
#include <beast/module/core/memory/SharedSingleton.h>
#include <beast/module/core/misc/WindowsRegistry.h>
#include <beast/module/core/streams/MemoryOutputStream.h>

#include <beast/module/core/system/SystemStats.h>
#include <beast/module/core/diagnostic/SemanticVersion.h>
#include <beast/module/core/threads/DynamicLibrary.h>
#include <beast/module/core/threads/Process.h>
#include <beast/module/core/diagnostic/UnitTestUtilities.h>

#include <beast/module/core/diagnostic/MeasureFunctionCallTime.h>

#include <beast/module/core/thread/DeadlineTimer.h>

#include <beast/module/core/thread/Workers.h>

#if BEAST_MSVC
#pragma warning (pop)
#endif

#endif
