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

#if defined (BEAST_CORE_BEASTHEADER) && ! BEAST_AMALGAMATED_INCLUDE
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of BEAST cpp file"
#endif

// Your project must contain a BeastConfig.h file with your project-specific settings in it,
// and your header search path must make it accessible to the module's files.
#include "BeastConfig.h"

//==============================================================================
#include "native/beast_BasicNativeHeaders.h"
#include "beast_core.h"

#include <locale>
#include <cctype>

#if ! BEAST_BSD
 #include <sys/timeb.h>
#endif

#if ! BEAST_ANDROID
 #include <cwctype>
#endif

#if BEAST_WINDOWS
 #include <ctime>
 #include <winsock2.h>
 #include <ws2tcpip.h>

 #if ! BEAST_MINGW
  #include <Dbghelp.h>

  #if ! BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES
   #pragma comment (lib, "DbgHelp.lib")
  #endif
 #endif

 #if BEAST_MINGW
  #include <ws2spi.h>
 #endif

#else
 #if BEAST_LINUX || BEAST_ANDROID
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/errno.h>
  #include <unistd.h>
  #include <netinet/in.h>
 #endif

 #if BEAST_LINUX
  #include <langinfo.h>
 #endif

 #include <pwd.h>
 #include <fcntl.h>
 #include <netdb.h>
 #include <arpa/inet.h>
 #include <netinet/tcp.h>
 #include <sys/time.h>
 #include <net/if.h>
 #include <sys/ioctl.h>

 #if ! BEAST_ANDROID && ! BEAST_BSD
  #include <execinfo.h>
 #endif
#endif

#if BEAST_MAC || BEAST_IOS
 #include <xlocale.h>
 #include <mach/mach.h>
#endif

#if BEAST_ANDROID
 #include <android/log.h>
#endif

//------------------------------------------------------------------------------

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

#include "containers/beast_AbstractFifo.cpp"
#include "containers/beast_DynamicObject.cpp"
#include "containers/beast_NamedValueSet.cpp"
#include "containers/beast_PropertySet.cpp"
#include "containers/beast_Variant.cpp"

#include "diagnostic/beast_Debug.cpp"
#include "diagnostic/beast_Error.cpp"
#include "diagnostic/beast_FPUFlags.cpp"
#include "diagnostic/beast_LeakChecked.cpp"
#include "diagnostic/beast_UnitTest.cpp"
#include "diagnostic/beast_UnitTestUtilities.cpp"

#include "files/beast_DirectoryIterator.cpp"
#include "files/beast_File.cpp"
#include "files/beast_FileInputStream.cpp"
#include "files/beast_FileOutputStream.cpp"
#include "files/beast_FileSearchPath.cpp"
#include "files/beast_RandomAccessFile.cpp"
#include "files/beast_TemporaryFile.cpp"

#include "json/beast_JSON.cpp"

#include "logging/beast_FileLogger.cpp"
#include "logging/beast_Logger.cpp"

#include "maths/beast_BigInteger.cpp"
#include "maths/beast_Expression.cpp"
#include "maths/beast_Random.cpp"

#include "memory/beast_MemoryBlock.cpp"

#include "misc/beast_Result.cpp"
#include "misc/beast_Uuid.cpp"

#include "network/beast_MACAddress.cpp"
#include "network/beast_NamedPipe.cpp"
#include "network/beast_Socket.cpp"
#include "network/beast_URL.cpp"
#include "network/beast_IPAddress.cpp"

#include "streams/beast_BufferedInputStream.cpp"
#include "streams/beast_FileInputSource.cpp"
#include "streams/beast_InputStream.cpp"
#include "streams/beast_MemoryInputStream.cpp"
#include "streams/beast_MemoryOutputStream.cpp"
#include "streams/beast_OutputStream.cpp"
#include "streams/beast_SubregionStream.cpp"

#include "system/beast_SystemStats.cpp"

#include "text/beast_CharacterFunctions.cpp"

#include "text/beast_Identifier.cpp"
#include "text/beast_LocalisedStrings.cpp"
#include "text/beast_String.cpp"
#include "text/beast_StringArray.cpp"
#include "text/beast_StringPairArray.cpp"
#include "text/beast_StringPool.cpp"
#include "text/beast_TextDiff.cpp"

#include "threads/beast_ChildProcess.cpp"
#include "threads/beast_ReadWriteLock.cpp"
#include "threads/beast_SpinDelay.cpp"
#include "threads/beast_Thread.cpp"
#include "threads/beast_ThreadPool.cpp"
#include "threads/beast_TimeSliceThread.cpp"

#include "time/beast_PerformanceCounter.cpp"
#include "time/beast_PerformedAtExit.cpp"
#include "time/beast_RelativeTime.cpp"
#include "time/beast_Time.cpp"

#include "xml/beast_XmlDocument.cpp"
#include "xml/beast_XmlElement.cpp"

#include "zip/beast_GZIPDecompressorInputStream.cpp"
#include "zip/beast_GZIPCompressorOutputStream.cpp"
#include "zip/beast_ZipFile.cpp"

#if BEAST_MAC || BEAST_IOS
#include "native/beast_osx_ObjCHelpers.h"
#endif

#if BEAST_WINDOWS
#include "native/beast_win32_FPUFlags.cpp"
#else
#include "native/beast_posix_FPUFlags.cpp"
#endif

#if BEAST_ANDROID
#include "native/beast_android_JNIHelpers.h"
#endif

#if ! BEAST_WINDOWS
#include "native/beast_posix_SharedCode.h"
#include "native/beast_posix_NamedPipe.cpp"
#endif

#if BEAST_MAC || BEAST_IOS
#include "native/beast_mac_Files.mm"
#include "native/beast_mac_Network.mm"
#include "native/beast_mac_Strings.mm"
#include "native/beast_mac_SystemStats.mm"
#include "native/beast_mac_Threads.mm"

#elif BEAST_WINDOWS
#include "native/beast_win32_ComSmartPtr.h"
#include "native/beast_win32_Files.cpp"
#include "native/beast_win32_Network.cpp"
#include "native/beast_win32_Registry.cpp"
#include "native/beast_win32_SystemStats.cpp"
#include "native/beast_win32_Threads.cpp"

#elif BEAST_LINUX
#include "native/beast_linux_Files.cpp"
#include "native/beast_linux_Network.cpp"
#include "native/beast_linux_SystemStats.cpp"
#include "native/beast_linux_Threads.cpp"

#elif BEAST_BSD
#include "native/beast_bsd_Files.cpp"
#include "native/beast_bsd_Network.cpp"
#include "native/beast_bsd_SystemStats.cpp"
#include "native/beast_bsd_Threads.cpp"

#elif BEAST_ANDROID
#include "native/beast_android_Files.cpp"
#include "native/beast_android_Misc.cpp"
#include "native/beast_android_Network.cpp"
#include "native/beast_android_SystemStats.cpp"
#include "native/beast_android_Threads.cpp"

#endif

#include "threads/beast_HighResolutionTimer.cpp"

}

#ifdef _CRTDBG_MAP_ALLOC
#pragma pop_macro("calloc")
#pragma pop_macro("free")
#pragma pop_macro("malloc")
#pragma pop_macro("realloc")
#pragma pop_macro("_recalloc")
#pragma pop_macro("_aligned_free")
#pragma pop_macro("_aligned_malloc")
#pragma pop_macro("_aligned_offset_malloc")
#pragma pop_macro("_aligned_realloc")
#pragma pop_macro("_aligned_recalloc")
#pragma pop_macro("_aligned_offset_realloc")
#pragma pop_macro("_aligned_offset_recalloc")
#pragma pop_macro("_aligned_msize")
#endif

//------------------------------------------------------------------------------

#if BEAST_BOOST_IS_AVAILABLE
namespace boost {
namespace placeholders {
boost::arg<1> _1;
boost::arg<2> _2;
boost::arg<3> _3;
boost::arg<4> _4;
boost::arg<5> _5;
boost::arg<6> _6;
boost::arg<7> _7;
boost::arg<8> _8;
boost::arg<9> _9;
}
}
#endif
