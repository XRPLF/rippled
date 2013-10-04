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
#include "../../beast/Config.h"
#include "../../beast/config/ContractChecks.h"

# include "system/BeforeBoost.h"
# include "system/BoostIncludes.h"
#include "system/FunctionalIncludes.h"

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
#include "../../beast/CStdInt.h"
#include "../../beast/SmartPtr.h"
#include "../../beast/StaticAssert.h"
#include "../../beast/Uncopyable.h"
#include "../../beast/Atomic.h"
#include "../../beast/Arithmetic.h"
#include "../../beast/ByteOrder.h"
#include "../../beast/HeapBlock.h"
#include "../../beast/Memory.h"
#include "../../beast/Intrusive.h"
#include "../../beast/Net.h"
#include "../../beast/SafeBool.h"
#include "../../beast/Strings.h"
#include "../../beast/TypeTraits.h"
#include "../../beast/Thread.h"
#include "../../beast/Utility.h"
#include "../../beast/Chrono.h"

#include "system/StandardIncludes.h"

namespace beast {

class InputStream;
class OutputStream;
class FileInputStream;
class FileOutputStream;

// Order matters, since headers don't have their own #include lines.
// Add new includes to the bottom.

#include "diagnostic/Throw.h"
#include "system/Functional.h"
#include "memory/AtomicCounter.h"
#include "memory/AtomicFlag.h"
#include "memory/AtomicPointer.h"
#include "memory/AtomicState.h"
#include "threads/SpinDelay.h"
#include "memory/StaticObject.h"

#include "time/AtExitHook.h"
#include "diagnostic/LeakChecked.h"
#include "time/Time.h"
#include "threads/ScopedLock.h"
#include "threads/CriticalSection.h"
#include "containers/ElementComparator.h"

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
#include "containers/ArrayAllocationBase.h"
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

#include "containers/Array.h"

#include "misc/Result.h"
#include "text/StringArray.h"
#include "memory/MemoryBlock.h"
#include "files/File.h"
#include "time/PerformanceCounter.h"

#include "memory/MemoryAlignment.h"
#include "memory/CacheLine.h"
#include "threads/ReadWriteMutex.h"
#include "threads/Thread.h"
#include "thread/MutexTraits.h"
#include "thread/TrackedMutex.h"
#include "diagnostic/FatalError.h"
#include "text/LexicalCast.h"
#include "maths/Math.h"
#include "maths/uint24.h"
#include "logging/Logger.h"
#include "diagnostic/FPUFlags.h"
#include "memory/SharedObject.h"
#include "memory/SharedPtr.h"
#include "memory/SharedFunction.h"
#include "containers/AbstractFifo.h"
#include "text/Identifier.h"
#include "containers/Variant.h"
#include "containers/LinkedListPointer.h"
#include "containers/NamedValueSet.h"
#include "containers/DynamicObject.h"
#include "maths/BigInteger.h"
#include "maths/Random.h"
#include "containers/LockFreeQueue.h"
#include "containers/OwnedArray.h"
#include "text/StringPairArray.h"
#include "containers/PropertySet.h"
#include "containers/SharedObjectArray.h"
#include "containers/ScopedValueSetter.h"
#include "containers/SortedSet.h"
#include "maths/Range.h"
#include "containers/SparseSet.h"
#include "files/DirectoryIterator.h"
#include "streams/InputStream.h"
#include "files/FileInputStream.h"
#include "streams/InputSource.h"
#include "streams/FileInputSource.h"
#include "streams/OutputStream.h"
#include "files/FileOutputStream.h"
#include "files/FileSearchPath.h"
#include "files/MemoryMappedFile.h"
#include "files/RandomAccessFile.h"
#include "files/TemporaryFile.h"
#include "json/JSON.h"
#include "logging/FileLogger.h"
#include "logging/Logger.h"
#include "maths/Expression.h"
#include "maths/Interval.h"
#include "maths/MurmurHash.h"
#include "memory/OptionalScopedPointer.h"
#include "memory/SharedSingleton.h"
#include "memory/WeakReference.h"
#include "memory/RecycledObjectPool.h"
#include "misc/Main.h"
#include "misc/Uuid.h"
#include "misc/WindowsRegistry.h"
#include "network/IPAddress.h"
#include "network/MACAddress.h"
#include "threads/ReadWriteLock.h"
#include "network/NamedPipe.h"
#include "network/Socket.h"
#include "streams/BufferedInputStream.h"
#include "streams/MemoryInputStream.h"
#include "streams/MemoryOutputStream.h"
#include "streams/SubregionStream.h"

#include "system/SystemStats.h"
#include "text/LocalisedStrings.h"
#include "diagnostic/SemanticVersion.h"
#include "text/StringPool.h"
#include "text/TextDiff.h"
#include "threads/ChildProcess.h"
#include "threads/DynamicLibrary.h"
#include "threads/HighResolutionTimer.h"
#include "threads/InterProcessLock.h"
#include "threads/Process.h"
#include "threads/ScopedReadLock.h"
#include "threads/ScopedWriteLock.h"
#include "threads/ThreadPool.h"
#include "threads/TimeSliceThread.h"
#include "diagnostic/UnitTest.h"
#include "xml/XmlDocument.h"
#include "xml/XmlElement.h"
#include "diagnostic/UnitTestUtilities.h"
#include "zip/GZIPCompressorOutputStream.h"
#include "zip/GZIPDecompressorInputStream.h"
#include "zip/ZipFile.h"

#include "diagnostic/MeasureFunctionCallTime.h"

#include "thread/DeadlineTimer.h"

#include "thread/Semaphore.h"
#include "thread/Stoppable.h"
#include "thread/Workers.h"

}

#if BEAST_MSVC
#pragma warning (pop)
#endif

#endif
