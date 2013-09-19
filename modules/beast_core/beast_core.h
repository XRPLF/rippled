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
/**

@mainpage Beast: A C++ library for peer to peer and client server development.

### Version 1.0

Copyright 2008, 2013 by Vinnie Falco \<vinnie.falco@gmail.com\> ([e-mail][0])

Beast is a source code collection of individual modules containing
functionality for a variety of applications, with an emphasis on building
concurrent systems. Beast incorporates parts of [JUCE][3] (Jules' Utility
Class Extensions), available from [Raw Material Software][4]. Beast has no
external dependencies 

Beast is hosted on Github at [https://github.com/vinniefalco/Beast][1]

The online documentation is at [http://vinniefalco.github.com/Beast][2]

## Platforms

All platforms supported by JUCE are also supported by Beast. Currently these
platforms include:

- **Windows**: Applications and VST/RTAS/NPAPI/ActiveX plugins can be built
using MS Visual Studio. The results are all fully compatible with Windows
XP, Vista or Windows 7.

- **Mac OS X**: Applications and VST/AudioUnit/RTAS/NPAPI plugins with Xcode.

- **GNU/Linux**: Applications and plugins can be built for any kernel 2.6 or
later.

- **FreeBSD**: Kernel version 8.4 or higher required.

- **iOS**: Native iPhone and iPad apps.

- **Android**: Supported.

## Prerequisites

This documentation assumes that the reader has a working knowledge of JUCE.
Some modules built on external libraries assume that the reader understands
the operation of those external libraries. Certain modules assume that the
reader understands additional domain-specific information. Modules with
additional prerequisites are marked in the documentation.

## External Modules

Some modules bring in functionality provided by external libraries. For
example, the @ref beast_bzip2 module provides the compression and decompression
algorithms in [bZip2][7]. Usage of these external library modules is optional.
They come with complete source code, as well as options for using either
system or user provided variants of the external libraries: it is not
necessary to download additional source code packages to use these modules.

External code incorporated into Beast is covered by separate licenses. See
the licensing information and notes in the corresponding source files for
copyright information and terms of use.

## Integration

Beast requires recent versions of JUCE. It won't work with versions 1.53 or
earlier. To use the library it is necessary to first download JUCE to a
location where your development environment can find it. Or, you can use your
existing installation of JUCE.

This library uses the same modularized organizational structure as JUCE. To
use a module, first add a path to the list of includes searched by your
development environment or project, which points to the Beast directory. Then,
add the single corresponding .c or .cpp file to your existing project which
already uses JUCE. For example, to use the @ref beast_core module, add the file
beast_core.cpp to your project. Some modules depend on other modules.

To use a module, include the appropriate header from within your source code.
For example, to access classes in the @ref beast_concurrent module, use this:

@code

#include "modules/beast_concurrent/beast_concurrent.h"

@endcode

Then add the corresponding file beast_concurrent.cpp to your build.

## AppConfig

Some Beast features can be controlled at compilation time through
preprocessor directives. The available choices of compilation options are
described in AppConfig.h, located in the AppConfigTemplate directory. Copy
the provided settings into your existing AppConfig.h (a file used by JUCE
convention).

## License

This library contains portions of other open source products covered by
separate licenses. Please see the corresponding source files for specific
terms.

Beast is provided under the terms of The ISC License:

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

Some files contain portions of these external projects, licensed separately:

- [bZip2][7] is Copyright (C) 1996-2010 Julian R Seward. All rights
    reserved. See the corresponding file LICENSE for licensing terms.

- [Soci][13] is Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton, and
    various others noted in the corresponding source files. Soci is distributed
    under the [Boost Software License, Version 1.0][14].

- [SQLite][15], placed in the public domain.

[0]: mailto:vinnie.falco@gmail.com "Vinnie Falco (Email)"
[1]: https://github.com/vinniefalco/Beast "Beast Project"
[2]: http://vinniefalco.github.com/Beast/ "Beast Documentation"
[3]: http://rawmaterialsoftware.com/juce.php "JUCE"
[4]: http://rawmaterialsoftware.com/ "Raw Material Software"
[5]: http://www.gnu.org/licenses/gpl-2.0.html "GNU General Public License, version 2"
[6]: http://rawmaterialsoftware.com/jucelicense.php "JUCE Licenses"
[7]: http://www.bzip.org/ "bZip2: Home"
[8]: http://freetype.org/ "The FreeType Project"
[9]: http://www.freetype.org/FTL.TXT "The FreeType Project License"
[10]: http://www.lua.org/ "The Programming Language Lua"
[11]: http://opensource.org/licenses/ISC "The ISC License"
[12]: https://github.com/vinniefalco/LuaBridge
[13]: http://soci.sourceforge.net/ "SOCI"
[14]: http://www.boost.org/LICENSE_1_0.txt "Boost Software License, Version 1.0"
[15]: http://sqlite.org/ "SQLite Home Page"
[16]: http://developer.kde.org/~wheeler/taglib.html "TagLib"
[17]: http://www.gnu.org/licenses/lgpl-2.1.html "Gnu Lesser General Public License, version 2.1"
[18]: http://www.mozilla.org/MPL/1.1/ "Mozilla Public License"

@copyright Copyright 2008-2013 by Vinnie Falco \<vinnie.falco@gmail.com\> ([e-mail][0])
@copyright Provided under the [ISC LIcense][11]
*/

// TargetPlatform.h should not use anything from BeastConfig.h
#include "system/TargetPlatform.h"
#include "system/BeastConfigCheck.h"

# include "system/BeforeBoost.h"
# include "system/BoostIncludes.h"
#include "system/FunctionalIncludes.h"
#include "system/PlatformDefs.h"
#include "system/StandardHeader.h"

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

//------------------------------------------------------------------------------

// New header-only library modeled more closely according to boost
#include "../../beast/intrusive/ForwardList.h"

//------------------------------------------------------------------------------

namespace beast
{

class InputStream;
class OutputStream;
class FileInputStream;
class FileOutputStream;

extern BEAST_API bool BEAST_CALLTYPE beast_isRunningUnderDebugger();
extern BEAST_API void BEAST_CALLTYPE logAssertion (char const* file, int line) noexcept;

// Order matters, since headers don't have their own #include lines.
// Add new includes to the bottom.

#include "diagnostic/ContractChecks.h"
#include "memory/Uncopyable.h"
#include "memory/Memory.h"
#include "maths/MathsFunctions.h"
#include "memory/ByteOrder.h"
#include "memory/Atomic.h"
#include "text/CharacterFunctions.h"

#if BEAST_MSVC
# pragma warning (push)
# pragma warning (disable: 4514 4996)
#endif
#include "text/CharPointer_UTF8.h"
#include "text/CharPointer_UTF16.h"
#include "text/CharPointer_UTF32.h"
#include "text/CharPointer_ASCII.h"
#if BEAST_MSVC
# pragma warning (pop)
#endif

# include "containers/detail/removecv.h"
#include "containers/detail/copyconst.h"
#include "diagnostic/Throw.h"
#include "system/Functional.h"
#include "memory/AtomicCounter.h"
#include "memory/AtomicFlag.h"
#include "memory/AtomicPointer.h"
#include "memory/AtomicState.h"
#include "containers/List.h"
#include "containers/LockFreeStack.h"
#include "threads/SpinDelay.h"
#include "memory/StaticObject.h"

#include "text/StringCharPointerType.h"
#include "text/StringFromNumber.h"
#include "text/String.h"
#include "time/AtExitHook.h"
#include "diagnostic/LeakChecked.h"
#include "diagnostic/Journal.h"
#include "time/RelativeTime.h"
#include "time/Time.h"
#include "memory/HeapBlock.h"
#include "threads/ScopedLock.h"
#include "threads/CriticalSection.h"
#include "containers/ElementComparator.h"
#include "containers/ArrayAllocationBase.h"
#include "containers/Array.h"
#include "misc/Result.h"
#include "text/StringArray.h"
#include "memory/MemoryBlock.h"
#include "files/File.h"
#include "time/PerformanceCounter.h"

#include "memory/MemoryAlignment.h"
#include "memory/CacheLine.h"
#include "threads/ReadWriteMutex.h"
#include "threads/SharedData.h"
#include "diagnostic/SafeBool.h"
#include "threads/WaitableEvent.h"
#include "threads/Thread.h"
#include "threads/SpinLock.h"
#include "threads/ThreadLocalValue.h"
#include "thread/MutexTraits.h"
#include "thread/TrackedMutex.h"
#include "diagnostic/FatalError.h"
#include "diagnostic/Error.h"
#include "diagnostic/Debug.h"
#include "text/LexicalCast.h"
#include "memory/ContainerDeletePolicy.h"
#include "memory/ByteSwap.h"
#include "maths/Math.h"
#include "maths/uint24.h"
#include "logging/Logger.h"
#include "diagnostic/FPUFlags.h"
#include "memory/SharedObject.h"
#include "memory/SharedPtr.h"
#include "memory/SharedFunction.h"
#include "diagnostic/ProtectedCall.h"
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
#include "containers/SharedTable.h"
#include "containers/SortedLookupTable.h"
#include "containers/SortedSet.h"
#include "maths/Range.h"
#include "containers/SparseSet.h"
# include "containers/DynamicList.h"
# include "containers/DynamicArray.h"
#include "containers/HashMap.h"
#include "memory/ScopedPointer.h"
#include "files/DirectoryIterator.h"
#include "streams/InputStream.h"
#include "files/FileInputStream.h"
#include "streams/InputSource.h"
#include "streams/FileInputSource.h"
#include "text/NewLine.h"
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
#include "maths/MathsFunctions.h"
#include "maths/MurmurHash.h"
#include "memory/ByteOrder.h"
#include "memory/Memory.h"
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
#include "network/URL.h"
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

#include "memory/AllocatedBy.h"
#include "memory/PagedFreeStore.h"
#include "memory/GlobalPagedFreeStore.h"
#include "memory/FifoFreeStoreWithTLS.h"
#include "memory/FifoFreeStoreWithoutTLS.h"
#include "memory/FifoFreeStore.h"
#include "memory/GlobalFifoFreeStore.h"

#include "thread/Semaphore.h"
#include "thread/InterruptibleThread.h"
#include "thread/CallQueue.h"
#include "thread/Listeners.h"
#include "thread/ManualCallQueue.h"
#include "thread/ThreadWithCallQueue.h"
#include "thread/Workers.h"

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

#endif
