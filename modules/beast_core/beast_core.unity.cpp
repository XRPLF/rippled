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

#if BEAST_INCLUDE_BEASTCONFIG
#include "../../BeastConfig.h"
#endif

//==============================================================================
#include "native/BasicNativeHeaders.h"
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

#include "diagnostic/FatalError.cpp"
#include "diagnostic/SemanticVersion.cpp"
#include "diagnostic/UnitTestUtilities.cpp"

#include "files/DirectoryIterator.cpp"
#include "files/File.cpp"
#include "files/FileInputStream.cpp"
#include "files/FileOutputStream.cpp"
#include "files/FileSearchPath.cpp"
#include "files/RandomAccessFile.cpp"
#include "files/TemporaryFile.cpp"

#include "logging/Logger.cpp"

#include "maths/Random.cpp"

#include "memory/MemoryBlock.cpp"

#include "misc/Result.cpp"

#include "streams/FileInputSource.cpp"
#include "streams/InputStream.cpp"
#include "streams/MemoryOutputStream.cpp"
#include "streams/OutputStream.cpp"

#include "system/SystemStats.cpp"

#include "text/LexicalCast.cpp"
#include "text/StringArray.cpp"
#include "text/StringPairArray.cpp"

#include "thread/DeadlineTimer.cpp"
#include "thread/Workers.cpp"

#include "time/AtExitHook.cpp"
#include "time/Time.cpp"

#if BEAST_MAC || BEAST_IOS
#include "native/osx_ObjCHelpers.h"
#endif

#if BEAST_ANDROID
#include "native/android_JNIHelpers.h"
#endif

#if ! BEAST_WINDOWS
#include "native/posix_SharedCode.h"
#endif

#if BEAST_MAC || BEAST_IOS
#include "native/mac_Files.mm"
#include "native/mac_Strings.mm"
#include "native/mac_SystemStats.mm"
#include "native/mac_Threads.mm"

#elif BEAST_WINDOWS
#include "native/win32_ComSmartPtr.h"
#include "native/win32_Files.cpp"
#include "native/win32_Registry.cpp"
#include "native/win32_SystemStats.cpp"
#include "native/win32_Threads.cpp"

#elif BEAST_LINUX
#include "native/linux_Files.cpp"
#include "native/linux_SystemStats.cpp"
#include "native/linux_Threads.cpp"

#elif BEAST_BSD
#include "native/bsd_Files.cpp"
#include "native/bsd_SystemStats.cpp"
#include "native/bsd_Threads.cpp"

#elif BEAST_ANDROID
#include "native/android_Files.cpp"
#include "native/android_Misc.cpp"
#include "native/android_SystemStats.cpp"
#include "native/android_Threads.cpp"

#endif

// Has to be outside the beast namespace
extern "C" {
void beast_reportFatalError (char const* message, char const* fileName, int lineNumber)
{
    if (beast::beast_isRunningUnderDebugger())
        beast_breakDebugger;
    beast::FatalError (message, fileName, lineNumber);
    BEAST_ANALYZER_NORETURN
}
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

// Must be outside the namespace
#include "system/BoostPlaceholdersFix.cpp"
