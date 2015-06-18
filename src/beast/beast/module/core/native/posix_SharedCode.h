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

#ifndef BEAST_MODULE_CORE_NATIVE_POSIX_SHAREDCODE_H_INCLUDED
#define BEAST_MODULE_CORE_NATIVE_POSIX_SHAREDCODE_H_INCLUDED

namespace beast
{

CriticalSection::CriticalSection() noexcept
{
    pthread_mutexattr_t atts;
    pthread_mutexattr_init (&atts);
    pthread_mutexattr_settype (&atts, PTHREAD_MUTEX_RECURSIVE);
   #if ! BEAST_ANDROID
    pthread_mutexattr_setprotocol (&atts, PTHREAD_PRIO_INHERIT);
   #endif
    pthread_mutex_init (&mutex, &atts);
    pthread_mutexattr_destroy (&atts);
}

CriticalSection::~CriticalSection() noexcept    { pthread_mutex_destroy (&mutex); }
void CriticalSection::enter() const noexcept    { pthread_mutex_lock (&mutex); }
bool CriticalSection::tryEnter() const noexcept { return pthread_mutex_trylock (&mutex) == 0; }
void CriticalSection::exit() const noexcept     { pthread_mutex_unlock (&mutex); }

//==============================================================================
const beast_wchar File::separator = '/';
const String File::separatorString ("/");

//==============================================================================
File File::getCurrentWorkingDirectory()
{
    HeapBlock<char> heapBuffer;

    char localBuffer [1024];
    char* cwd = getcwd (localBuffer, sizeof (localBuffer) - 1);
    size_t bufferSize = 4096;

    while (cwd == nullptr && errno == ERANGE)
    {
        heapBuffer.malloc (bufferSize);
        cwd = getcwd (heapBuffer, bufferSize - 1);
        bufferSize += 1024;
    }

    return File (CharPointer_UTF8 (cwd));
}

// if this file doesn't exist, find a parent of it that does..
inline
bool beast_doStatFS (File f, struct statfs& result)
{
    for (int i = 5; --i >= 0;)
    {
        if (f.exists())
            break;

        f = f.getParentDirectory();
    }

    return statfs (f.getFullPathName().toUTF8(), &result) == 0;
}

//==============================================================================
namespace
{
   #if BEAST_LINUX || \
       (BEAST_IOS && ! __DARWIN_ONLY_64_BIT_INO_T) // (this iOS stuff is to avoid a simulator bug)
    using beast_statStruct = struct stat64;
    #define BEAST_STAT     stat64
   #else
    using beast_statStruct = struct stat;
    #define BEAST_STAT     stat
   #endif

    bool beast_stat (const String& fileName, beast_statStruct& info)
    {
        return fileName.isNotEmpty()
                 && BEAST_STAT (fileName.toUTF8(), &info) == 0;
    }

    void updateStatInfoForFile (const String& path, bool* const isDir, std::int64_t* const fileSize,
                                Time* const modTime, Time* const creationTime, bool* const isReadOnly)
    {
        if (isDir != nullptr || fileSize != nullptr || modTime != nullptr || creationTime != nullptr)
        {
            beast_statStruct info;
            const bool statOk = beast_stat (path, info);

            if (isDir != nullptr)         *isDir        = statOk && ((info.st_mode & S_IFDIR) != 0);
            if (fileSize != nullptr)      *fileSize     = statOk ? info.st_size : 0;
            if (modTime != nullptr)       *modTime      = Time (statOk ? (std::int64_t) info.st_mtime * 1000 : 0);
            if (creationTime != nullptr)  *creationTime = Time (statOk ? (std::int64_t) info.st_ctime * 1000 : 0);
        }

        if (isReadOnly != nullptr)
            *isReadOnly = access (path.toUTF8(), W_OK) != 0;
    }

    Result getResultForErrno()
    {
        return Result::fail (String (strerror (errno)));
    }

    Result getResultForReturnValue (int value)
    {
        return value == -1 ? getResultForErrno() : Result::ok();
    }

    int getFD (void* handle) noexcept        { return (int) (std::intptr_t) handle; }
    void* fdToVoidPointer (int fd) noexcept  { return (void*) (std::intptr_t) fd; }
}

bool File::isDirectory() const
{
    beast_statStruct info;

    return fullPath.isEmpty()
            || (beast_stat (fullPath, info) && ((info.st_mode & S_IFDIR) != 0));
}

bool File::exists() const
{
    return fullPath.isNotEmpty()
             && access (fullPath.toUTF8(), F_OK) == 0;
}

bool File::existsAsFile() const
{
    return exists() && ! isDirectory();
}

std::int64_t File::getSize() const
{
    beast_statStruct info;
    return beast_stat (fullPath, info) ? info.st_size : 0;
}

//==============================================================================
bool File::deleteFile() const
{
    if (! exists())
        return true;

    if (isDirectory())
        return rmdir (fullPath.toUTF8()) == 0;

    return remove (fullPath.toUTF8()) == 0;
}

Result File::createDirectoryInternal (const String& fileName) const
{
    return getResultForReturnValue (mkdir (fileName.toUTF8(), 0777));
}

//=====================================================================
std::int64_t beast_fileSetPosition (void* handle, std::int64_t pos)
{
    if (handle != 0 && lseek (getFD (handle), pos, SEEK_SET) == pos)
        return pos;

    return -1;
}

void FileInputStream::openHandle()
{
    const int f = open (file.getFullPathName().toUTF8(), O_RDONLY, 00644);

    if (f != -1)
        fileHandle = fdToVoidPointer (f);
    else
        status = getResultForErrno();
}

void FileInputStream::closeHandle()
{
    if (fileHandle != 0)
    {
        close (getFD (fileHandle));
        fileHandle = 0;
    }
}

size_t FileInputStream::readInternal (void* const buffer, const size_t numBytes)
{
    std::ptrdiff_t result = 0;

    if (fileHandle != 0)
    {
        result = ::read (getFD (fileHandle), buffer, numBytes);

        if (result < 0)
        {
            status = getResultForErrno();
            result = 0;
        }
    }

    return (size_t) result;
}

//==============================================================================
void FileOutputStream::openHandle()
{
    if (file.exists())
    {
        const int f = open (file.getFullPathName().toUTF8(), O_RDWR, 00644);

        if (f != -1)
        {
            currentPosition = lseek (f, 0, SEEK_END);

            if (currentPosition >= 0)
            {
                fileHandle = fdToVoidPointer (f);
            }
            else
            {
                status = getResultForErrno();
                close (f);
            }
        }
        else
        {
            status = getResultForErrno();
        }
    }
    else
    {
        const int f = open (file.getFullPathName().toUTF8(), O_RDWR + O_CREAT, 00644);

        if (f != -1)
            fileHandle = fdToVoidPointer (f);
        else
            status = getResultForErrno();
    }
}

void FileOutputStream::closeHandle()
{
    if (fileHandle != 0)
    {
        close (getFD (fileHandle));
        fileHandle = 0;
    }
}

std::ptrdiff_t FileOutputStream::writeInternal (const void* const data, const size_t numBytes)
{
    std::ptrdiff_t result = 0;

    if (fileHandle != 0)
    {
        result = ::write (getFD (fileHandle), data, numBytes);

        if (result == -1)
            status = getResultForErrno();
    }

    return result;
}

void FileOutputStream::flushInternal()
{
    if (fileHandle != 0)
    {
        if (fsync (getFD (fileHandle)) == -1)
            status = getResultForErrno();

       #if BEAST_ANDROID
        // This stuff tells the OS to asynchronously update the metadata
        // that the OS has cached aboud the file - this metadata is used
        // when the device is acting as a USB drive, and unless it's explicitly
        // refreshed, it'll get out of step with the real file.
        const LocalRef<jstring> t (javaString (file.getFullPathName()));
        android.activity.callVoidMethod (BeastAppActivity.scanFile, t.get());
       #endif
    }
}

Result FileOutputStream::truncate()
{
    if (fileHandle == 0)
        return status;

    flush();
    return getResultForReturnValue (ftruncate (getFD (fileHandle), (off_t) currentPosition));
}

//==============================================================================
#if BEAST_PROBEASTR_LIVE_BUILD
extern "C" const char* beast_getCurrentExecutablePath();
#endif

File beast_getExecutableFile();
File beast_getExecutableFile()
{
   #if BEAST_PROBEASTR_LIVE_BUILD
    return File (beast_getCurrentExecutablePath());
   #elif BEAST_ANDROID
    return File (android.appFile);
   #else
    struct DLAddrReader
    {
        static String getFilename()
        {
            Dl_info exeInfo;
            dladdr ((void*) beast_getExecutableFile, &exeInfo);
            return CharPointer_UTF8 (exeInfo.dli_fname);
        }
    };

    static String filename (DLAddrReader::getFilename());
    return File::getCurrentWorkingDirectory().getChildFile (filename);
   #endif
}

} // beast
#endif
