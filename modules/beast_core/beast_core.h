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

#ifndef BEAST_CORE_BEASTHEADER
#define BEAST_CORE_BEASTHEADER

#ifndef BEAST_BEASTCONFIG_HEADER
 /* If you fail to make sure that all your compile units are building Beast with the same set of
    option flags, then there's a risk that different compile units will treat the classes as having
    different memory layouts, leading to very nasty memory corruption errors when they all get
    linked together. That's why it's best to always include the BeastConfig.h file before any beast headers.
 */
 #ifdef _MSC_VER
#pragma message ("Have you included your BeastConfig.h file before including the Beast headers?")
 #else
  #warning "Have you included your BeastConfig.h file before including the Beast headers?"
 #endif
#endif

//==============================================================================
#include "system/beast_TargetPlatform.h"

//=============================================================================
/** Config: BEAST_FORCE_DEBUG

    Normally, BEAST_DEBUG is set to 1 or 0 based on compiler and project settings,
    but if you define this value, you can override this to force it to be true or false.
*/
#ifndef BEAST_FORCE_DEBUG
 //#define BEAST_FORCE_DEBUG 0
#endif

//=============================================================================
/** Config: BEAST_LOG_ASSERTIONS

    If this flag is enabled, the the bassert and jassertfalse macros will always use Logger::writeToLog()
    to write a message when an assertion happens.

    Enabling it will also leave this turned on in release builds. When it's disabled,
    however, the bassert and jassertfalse macros will not be compiled in a
    release build.

    @see bassert, jassertfalse, Logger
*/
#ifndef BEAST_LOG_ASSERTIONS
 #if BEAST_ANDROID
  #define BEAST_LOG_ASSERTIONS 1
 #else
  #define BEAST_LOG_ASSERTIONS 0
 #endif
#endif

//=============================================================================
/** Config: BEAST_CHECK_MEMORY_LEAKS

    Enables a memory-leak check for certain objects when the app terminates. See the LeakedObjectDetector
    class and the BEAST_LEAK_DETECTOR macro for more details about enabling leak checking for specific classes.
*/
#if BEAST_DEBUG && ! defined (BEAST_CHECK_MEMORY_LEAKS)
 #define BEAST_CHECK_MEMORY_LEAKS 1
#endif

//=============================================================================
/** Config: BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES

    In a Visual C++  build, this can be used to stop the required system libs being
    automatically added to the link stage.
*/
#ifndef BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
 #define BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES 0
#endif

/*  Config: BEAST_INCLUDE_ZLIB_CODE
    This can be used to disable Beast's embedded 3rd-party zlib code.
    You might need to tweak this if you're linking to an external zlib library in your app,
    but for normal apps, this option should be left alone.

    If you disable this, you might also want to set a value for BEAST_ZLIB_INCLUDE_PATH, to
    specify the path where your zlib headers live.
*/
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

//=============================================================================
//=============================================================================
#if BEAST_MSVC
 #pragma warning (disable: 4251) // (DLL build warning, must be disabled before pushing the warning state)
 #pragma warning (push)
 #pragma warning (disable: 4786) // (long class name warning)
 #ifdef __INTEL_COMPILER
  #pragma warning (disable: 1125)
 #endif
#endif

#include "system/beast_StandardHeader.h"

namespace beast
{

// START_AUTOINCLUDE containers, files, json, logging, maths, memory, misc, network,
// streams, system, text, threads, time, unit_tests, xml, zip
#ifndef BEAST_ABSTRACTFIFO_BEASTHEADER
 #include "containers/beast_AbstractFifo.h"
#endif
#ifndef BEAST_ARRAY_BEASTHEADER
 #include "containers/beast_Array.h"
#endif
#ifndef BEAST_ARRAYALLOCATIONBASE_BEASTHEADER
 #include "containers/beast_ArrayAllocationBase.h"
#endif
#ifndef BEAST_DYNAMICOBJECT_BEASTHEADER
 #include "containers/beast_DynamicObject.h"
#endif
#ifndef BEAST_ELEMENTCOMPARATOR_BEASTHEADER
 #include "containers/beast_ElementComparator.h"
#endif
#ifndef BEAST_HASHMAP_BEASTHEADER
 #include "containers/beast_HashMap.h"
#endif
#ifndef BEAST_LINKEDLISTPOINTER_BEASTHEADER
 #include "containers/beast_LinkedListPointer.h"
#endif
#ifndef BEAST_NAMEDVALUESET_BEASTHEADER
 #include "containers/beast_NamedValueSet.h"
#endif
#ifndef BEAST_OWNEDARRAY_BEASTHEADER
 #include "containers/beast_OwnedArray.h"
#endif
#ifndef BEAST_PROPERTYSET_BEASTHEADER
 #include "containers/beast_PropertySet.h"
#endif
#ifndef BEAST_REFERENCECOUNTEDARRAY_BEASTHEADER
 #include "containers/beast_ReferenceCountedArray.h"
#endif
#ifndef BEAST_SCOPEDVALUESETTER_BEASTHEADER
 #include "containers/beast_ScopedValueSetter.h"
#endif
#ifndef BEAST_SORTEDSET_BEASTHEADER
 #include "containers/beast_SortedSet.h"
#endif
#ifndef BEAST_SPARSESET_BEASTHEADER
 #include "containers/beast_SparseSet.h"
#endif
#ifndef BEAST_VARIANT_BEASTHEADER
 #include "containers/beast_Variant.h"
#endif
#ifndef BEAST_DIRECTORYITERATOR_BEASTHEADER
 #include "files/beast_DirectoryIterator.h"
#endif
#ifndef BEAST_FILE_BEASTHEADER
 #include "files/beast_File.h"
#endif
#ifndef BEAST_FILEINPUTSTREAM_BEASTHEADER
 #include "files/beast_FileInputStream.h"
#endif
#ifndef BEAST_FILEOUTPUTSTREAM_BEASTHEADER
 #include "files/beast_FileOutputStream.h"
#endif
#ifndef BEAST_FILESEARCHPATH_BEASTHEADER
 #include "files/beast_FileSearchPath.h"
#endif
#ifndef BEAST_MEMORYMAPPEDFILE_BEASTHEADER
 #include "files/beast_MemoryMappedFile.h"
#endif
#ifndef BEAST_TEMPORARYFILE_BEASTHEADER
 #include "files/beast_TemporaryFile.h"
#endif
#ifndef BEAST_JSON_BEASTHEADER
 #include "json/beast_JSON.h"
#endif
#ifndef BEAST_FILELOGGER_BEASTHEADER
 #include "logging/beast_FileLogger.h"
#endif
#ifndef BEAST_LOGGER_BEASTHEADER
 #include "logging/beast_Logger.h"
#endif
#ifndef BEAST_BIGINTEGER_BEASTHEADER
 #include "maths/beast_BigInteger.h"
#endif
#ifndef BEAST_EXPRESSION_BEASTHEADER
 #include "maths/beast_Expression.h"
#endif
#ifndef BEAST_MATHSFUNCTIONS_BEASTHEADER
 #include "maths/beast_MathsFunctions.h"
#endif
#ifndef BEAST_RANDOM_BEASTHEADER
 #include "maths/beast_Random.h"
#endif
#ifndef BEAST_RANGE_BEASTHEADER
 #include "maths/beast_Range.h"
#endif
#ifndef BEAST_ATOMIC_BEASTHEADER
 #include "memory/beast_Atomic.h"
#endif
#ifndef BEAST_BYTEORDER_BEASTHEADER
 #include "memory/beast_ByteOrder.h"
#endif
#ifndef BEAST_HEAPBLOCK_BEASTHEADER
 #include "memory/beast_HeapBlock.h"
#endif
#ifndef BEAST_LEAKEDOBJECTDETECTOR_BEASTHEADER
 #include "memory/beast_LeakedObjectDetector.h"
#endif
#ifndef BEAST_MEMORY_BEASTHEADER
 #include "memory/beast_Memory.h"
#endif
#ifndef BEAST_MEMORYBLOCK_BEASTHEADER
 #include "memory/beast_MemoryBlock.h"
#endif
#ifndef BEAST_OPTIONALSCOPEDPOINTER_BEASTHEADER
 #include "memory/beast_OptionalScopedPointer.h"
#endif
#ifndef BEAST_REFERENCECOUNTEDOBJECT_BEASTHEADER
 #include "memory/beast_ReferenceCountedObject.h"
#endif
#ifndef BEAST_SCOPEDPOINTER_BEASTHEADER
 #include "memory/beast_ScopedPointer.h"
#endif
#ifndef BEAST_SINGLETON_BEASTHEADER
 #include "memory/beast_Singleton.h"
#endif
#ifndef BEAST_WEAKREFERENCE_BEASTHEADER
 #include "memory/beast_WeakReference.h"
#endif
#ifndef BEAST_RESULT_BEASTHEADER
 #include "misc/beast_Result.h"
#endif
#ifndef BEAST_UUID_BEASTHEADER
 #include "misc/beast_Uuid.h"
#endif
#ifndef BEAST_WINDOWSREGISTRY_BEASTHEADER
 #include "misc/beast_WindowsRegistry.h"
#endif
#ifndef BEAST_IPADDRESS_BEASTHEADER
 #include "network/beast_IPAddress.h"
#endif
#ifndef BEAST_MACADDRESS_BEASTHEADER
 #include "network/beast_MACAddress.h"
#endif
#ifndef BEAST_NAMEDPIPE_BEASTHEADER
 #include "network/beast_NamedPipe.h"
#endif
#ifndef BEAST_SOCKET_BEASTHEADER
 #include "network/beast_Socket.h"
#endif
#ifndef BEAST_URL_BEASTHEADER
 #include "network/beast_URL.h"
#endif
#ifndef BEAST_BUFFEREDINPUTSTREAM_BEASTHEADER
 #include "streams/beast_BufferedInputStream.h"
#endif
#ifndef BEAST_FILEINPUTSOURCE_BEASTHEADER
 #include "streams/beast_FileInputSource.h"
#endif
#ifndef BEAST_INPUTSOURCE_BEASTHEADER
 #include "streams/beast_InputSource.h"
#endif
#ifndef BEAST_INPUTSTREAM_BEASTHEADER
 #include "streams/beast_InputStream.h"
#endif
#ifndef BEAST_MEMORYINPUTSTREAM_BEASTHEADER
 #include "streams/beast_MemoryInputStream.h"
#endif
#ifndef BEAST_MEMORYOUTPUTSTREAM_BEASTHEADER
 #include "streams/beast_MemoryOutputStream.h"
#endif
#ifndef BEAST_OUTPUTSTREAM_BEASTHEADER
 #include "streams/beast_OutputStream.h"
#endif
#ifndef BEAST_SUBREGIONSTREAM_BEASTHEADER
 #include "streams/beast_SubregionStream.h"
#endif
#ifndef BEAST_PLATFORMDEFS_BEASTHEADER
 #include "system/beast_PlatformDefs.h"
#endif
#ifndef BEAST_STANDARDHEADER_BEASTHEADER
 #include "system/beast_StandardHeader.h"
#endif
#ifndef BEAST_SYSTEMSTATS_BEASTHEADER
 #include "system/beast_SystemStats.h"
#endif
#ifndef BEAST_TARGETPLATFORM_BEASTHEADER
 #include "system/beast_TargetPlatform.h"
#endif
#ifndef BEAST_CHARACTERFUNCTIONS_BEASTHEADER
 #include "text/beast_CharacterFunctions.h"
#endif
#ifndef BEAST_CHARPOINTER_ASCII_BEASTHEADER
 #include "text/beast_CharPointer_ASCII.h"
#endif
#ifndef BEAST_CHARPOINTER_UTF16_BEASTHEADER
 #include "text/beast_CharPointer_UTF16.h"
#endif
#ifndef BEAST_CHARPOINTER_UTF32_BEASTHEADER
 #include "text/beast_CharPointer_UTF32.h"
#endif
#ifndef BEAST_CHARPOINTER_UTF8_BEASTHEADER
 #include "text/beast_CharPointer_UTF8.h"
#endif
#ifndef BEAST_IDENTIFIER_BEASTHEADER
 #include "text/beast_Identifier.h"
#endif
#ifndef BEAST_LOCALISEDSTRINGS_BEASTHEADER
 #include "text/beast_LocalisedStrings.h"
#endif
#ifndef BEAST_NEWLINE_BEASTHEADER
 #include "text/beast_NewLine.h"
#endif
#ifndef BEAST_STRING_BEASTHEADER
 #include "text/beast_String.h"
#endif
#ifndef BEAST_STRINGARRAY_BEASTHEADER
 #include "text/beast_StringArray.h"
#endif
#ifndef BEAST_STRINGPAIRARRAY_BEASTHEADER
 #include "text/beast_StringPairArray.h"
#endif
#ifndef BEAST_STRINGPOOL_BEASTHEADER
 #include "text/beast_StringPool.h"
#endif
#ifndef BEAST_TEXTDIFF_BEASTHEADER
 #include "text/beast_TextDiff.h"
#endif
#ifndef BEAST_CHILDPROCESS_BEASTHEADER
 #include "threads/beast_ChildProcess.h"
#endif
#ifndef BEAST_CRITICALSECTION_BEASTHEADER
 #include "threads/beast_CriticalSection.h"
#endif
#ifndef BEAST_DYNAMICLIBRARY_BEASTHEADER
 #include "threads/beast_DynamicLibrary.h"
#endif
#ifndef BEAST_HIGHRESOLUTIONTIMER_BEASTHEADER
 #include "threads/beast_HighResolutionTimer.h"
#endif
#ifndef BEAST_INTERPROCESSLOCK_BEASTHEADER
 #include "threads/beast_InterProcessLock.h"
#endif
#ifndef BEAST_PROCESS_BEASTHEADER
 #include "threads/beast_Process.h"
#endif
#ifndef BEAST_READWRITELOCK_BEASTHEADER
 #include "threads/beast_ReadWriteLock.h"
#endif
#ifndef BEAST_SCOPEDLOCK_BEASTHEADER
 #include "threads/beast_ScopedLock.h"
#endif
#ifndef BEAST_SCOPEDREADLOCK_BEASTHEADER
 #include "threads/beast_ScopedReadLock.h"
#endif
#ifndef BEAST_SCOPEDWRITELOCK_BEASTHEADER
 #include "threads/beast_ScopedWriteLock.h"
#endif
#ifndef BEAST_SPINLOCK_BEASTHEADER
 #include "threads/beast_SpinLock.h"
#endif
#ifndef BEAST_THREAD_BEASTHEADER
 #include "threads/beast_Thread.h"
#endif
#ifndef BEAST_THREADLOCALVALUE_BEASTHEADER
 #include "threads/beast_ThreadLocalValue.h"
#endif
#ifndef BEAST_THREADPOOL_BEASTHEADER
 #include "threads/beast_ThreadPool.h"
#endif
#ifndef BEAST_TIMESLICETHREAD_BEASTHEADER
 #include "threads/beast_TimeSliceThread.h"
#endif
#ifndef BEAST_WAITABLEEVENT_BEASTHEADER
 #include "threads/beast_WaitableEvent.h"
#endif
#ifndef BEAST_PERFORMANCECOUNTER_BEASTHEADER
 #include "time/beast_PerformanceCounter.h"
#endif
#ifndef BEAST_RELATIVETIME_BEASTHEADER
 #include "time/beast_RelativeTime.h"
#endif
#ifndef BEAST_TIME_BEASTHEADER
 #include "time/beast_Time.h"
#endif
#ifndef BEAST_UNITTEST_BEASTHEADER
 #include "unit_tests/beast_UnitTest.h"
#endif
#ifndef BEAST_XMLDOCUMENT_BEASTHEADER
 #include "xml/beast_XmlDocument.h"
#endif
#ifndef BEAST_XMLELEMENT_BEASTHEADER
 #include "xml/beast_XmlElement.h"
#endif
#ifndef BEAST_GZIPCOMPRESSOROUTPUTSTREAM_BEASTHEADER
 #include "zip/beast_GZIPCompressorOutputStream.h"
#endif
#ifndef BEAST_GZIPDECOMPRESSORINPUTSTREAM_BEASTHEADER
 #include "zip/beast_GZIPDecompressorInputStream.h"
#endif
#ifndef BEAST_ZIPFILE_BEASTHEADER
 #include "zip/beast_ZipFile.h"
#endif
// END_AUTOINCLUDE

}

#if BEAST_MSVC
 #pragma warning (pop)
#endif

#endif   // BEAST_CORE_BEASTHEADER
