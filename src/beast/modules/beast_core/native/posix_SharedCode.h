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

void Process::terminate()
{
#if BEAST_ANDROID || BEAST_BSD
   // http://www.unix.com/man-page/FreeBSD/2/_exit/
    ::_exit (EXIT_FAILURE);
#else
    std::_Exit (EXIT_FAILURE);
#endif
}

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

bool File::setAsCurrentWorkingDirectory() const
{
    return chdir (getFullPathName().toUTF8()) == 0;
}

//==============================================================================
// The unix siginterrupt function is deprecated - this does the same job.
int beast_siginterrupt (int sig, int flag)
{
    struct ::sigaction act;
    (void) ::sigaction (sig, nullptr, &act);

    if (flag != 0)
        act.sa_flags &= ~SA_RESTART;
    else
        act.sa_flags |= SA_RESTART;

    return ::sigaction (sig, &act, nullptr);
}

//==============================================================================
namespace
{
   #if BEAST_LINUX || \
       (BEAST_IOS && ! __DARWIN_ONLY_64_BIT_INO_T) // (this iOS stuff is to avoid a simulator bug)
    typedef struct stat64 beast_statStruct;
    #define BEAST_STAT     stat64
   #else
    typedef struct stat   beast_statStruct;
    #define BEAST_STAT     stat
   #endif

    bool beast_stat (const String& fileName, beast_statStruct& info)
    {
        return fileName.isNotEmpty()
                 && BEAST_STAT (fileName.toUTF8(), &info) == 0;
    }

    // if this file doesn't exist, find a parent of it that does..
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

    void updateStatInfoForFile (const String& path, bool* const isDir, int64* const fileSize,
                                Time* const modTime, Time* const creationTime, bool* const isReadOnly)
    {
        if (isDir != nullptr || fileSize != nullptr || modTime != nullptr || creationTime != nullptr)
        {
            beast_statStruct info;
            const bool statOk = beast_stat (path, info);

            if (isDir != nullptr)         *isDir        = statOk && ((info.st_mode & S_IFDIR) != 0);
            if (fileSize != nullptr)      *fileSize     = statOk ? info.st_size : 0;
            if (modTime != nullptr)       *modTime      = Time (statOk ? (int64) info.st_mtime * 1000 : 0);
            if (creationTime != nullptr)  *creationTime = Time (statOk ? (int64) info.st_ctime * 1000 : 0);
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

    int getFD (void* handle) noexcept        { return (int) (pointer_sized_int) handle; }
    void* fdToVoidPointer (int fd) noexcept  { return (void*) (pointer_sized_int) fd; }
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

int64 File::getSize() const
{
    beast_statStruct info;
    return beast_stat (fullPath, info) ? info.st_size : 0;
}

//==============================================================================
bool File::hasWriteAccess() const
{
    if (exists())
        return access (fullPath.toUTF8(), W_OK) == 0;

    if ((! isDirectory()) && fullPath.containsChar (separator))
        return getParentDirectory().hasWriteAccess();

    return false;
}

bool File::setFileReadOnlyInternal (const bool shouldBeReadOnly) const
{
    beast_statStruct info;
    if (! beast_stat (fullPath, info))
        return false;

    info.st_mode &= 0777;   // Just permissions

    if (shouldBeReadOnly)
        info.st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    else
        // Give everybody write permission?
        info.st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;

    return chmod (fullPath.toUTF8(), info.st_mode) == 0;
}

void File::getFileTimesInternal (int64& modificationTime, int64& accessTime, int64& creationTime) const
{
    modificationTime = 0;
    accessTime = 0;
    creationTime = 0;

    beast_statStruct info;
    if (beast_stat (fullPath, info))
    {
        modificationTime = (int64) info.st_mtime * 1000;
        accessTime = (int64) info.st_atime * 1000;
        creationTime = (int64) info.st_ctime * 1000;
    }
}

bool File::setFileTimesInternal (int64 modificationTime, int64 accessTime, int64 /*creationTime*/) const
{
    beast_statStruct info;

    if ((modificationTime != 0 || accessTime != 0) && beast_stat (fullPath, info))
    {
        struct utimbuf times;
        times.actime  = accessTime != 0       ? (time_t) (accessTime / 1000)       : info.st_atime;
        times.modtime = modificationTime != 0 ? (time_t) (modificationTime / 1000) : info.st_mtime;

        return utime (fullPath.toUTF8(), &times) == 0;
    }

    return false;
}

bool File::deleteFile() const
{
    if (! exists())
        return true;

    if (isDirectory())
        return rmdir (fullPath.toUTF8()) == 0;

    return remove (fullPath.toUTF8()) == 0;
}

bool File::moveInternal (const File& dest) const
{
    if (rename (fullPath.toUTF8(), dest.getFullPathName().toUTF8()) == 0)
        return true;

    if (hasWriteAccess() && copyInternal (dest))
    {
        if (deleteFile())
            return true;

        dest.deleteFile();
    }

    return false;
}

Result File::createDirectoryInternal (const String& fileName) const
{
    return getResultForReturnValue (mkdir (fileName.toUTF8(), 0777));
}

//=====================================================================
int64 beast_fileSetPosition (void* handle, int64 pos)
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
    ssize_t result = 0;

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

ssize_t FileOutputStream::writeInternal (const void* const data, const size_t numBytes)
{
    ssize_t result = 0;

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

Result RandomAccessFile::nativeOpen (File const& path, Mode mode)
{
    bassert (! isOpen ());

    Result result (Result::ok ());

    if (path.exists())
    {
        int oflag;
        switch (mode)
        {
        case readOnly:
            oflag = O_RDONLY;
            break;

        default:
        case readWrite:
            oflag = O_RDWR;
            break;
        };

        const int f = ::open (path.getFullPathName().toUTF8(), oflag, 00644);

        if (f != -1)
        {
            currentPosition = lseek (f, 0, SEEK_SET);

            if (currentPosition >= 0)
            {
                file = path;
                fileHandle = fdToVoidPointer (f);
            }
            else
            {
                result = getResultForErrno();
                ::close (f);
            }
        }
        else
        {
            result = getResultForErrno();
        }
    }
    else if (mode == readWrite)
    {
        const int f = ::open (path.getFullPathName().toUTF8(), O_RDWR + O_CREAT, 00644);

        if (f != -1)
        {
            file = path;
            fileHandle = fdToVoidPointer (f);
        }
        else
        {
            result = getResultForErrno();
        }
    }
    else
    {
        // file doesn't exist and we're opening read-only
        Result::fail (String (strerror (ENOENT)));
    }

    return result;
}

void RandomAccessFile::nativeClose ()
{
    bassert (isOpen ());

    file = File::nonexistent ();
    ::close (getFD (fileHandle));
    fileHandle = nullptr;
    currentPosition = 0;
}

Result RandomAccessFile::nativeSetPosition (FileOffset newPosition)
{
    bassert (isOpen ());

    off_t const actualPosition = lseek (getFD (fileHandle), newPosition, SEEK_SET);

    currentPosition = actualPosition;

    if (actualPosition != newPosition)
    {
        // VFALCO NOTE I dislike return from the middle but
        //             Result::ok() is showing up in the profile
        //
        return getResultForErrno();
    }

    return Result::ok();
}

Result RandomAccessFile::nativeRead (void* buffer, ByteCount numBytes, ByteCount* pActualAmount)
{
    bassert (isOpen ());

    ssize_t bytesRead = ::read (getFD (fileHandle), buffer, numBytes);

    if (bytesRead < 0)
    {
        if (pActualAmount != nullptr)
            *pActualAmount = 0;

        // VFALCO NOTE I dislike return from the middle but
        //             Result::ok() is showing up in the profile
        //
        return getResultForErrno();
    }

    currentPosition += bytesRead;

    if (pActualAmount != nullptr)
        *pActualAmount = bytesRead;

    return Result::ok();
}

Result RandomAccessFile::nativeWrite (void const* data, ByteCount numBytes, size_t* pActualAmount)
{
    bassert (isOpen ());

    ssize_t bytesWritten = ::write (getFD (fileHandle), data, numBytes);

    // write(3) says that the actual return will be exactly -1 on
    // error, but we will assume anything negative indicates failure.
    //
    if (bytesWritten < 0)
    {
        if (pActualAmount != nullptr)
            *pActualAmount = 0;

        // VFALCO NOTE I dislike return from the middle but
        //             Result::ok() is showing up in the profile
        //
        return getResultForErrno();
    }

    if (pActualAmount != nullptr)
        *pActualAmount = bytesWritten;

    return Result::ok();
}

Result RandomAccessFile::nativeTruncate ()
{
    bassert (isOpen ());

    flush();

    return getResultForReturnValue (ftruncate (getFD (fileHandle), (off_t) currentPosition));
}

Result RandomAccessFile::nativeFlush ()
{
    bassert (isOpen ());

    Result result (Result::ok ());

    if (fsync (getFD (fileHandle)) == -1)
        result = getResultForErrno();

#if BEAST_ANDROID
    // This stuff tells the OS to asynchronously update the metadata
    // that the OS has cached aboud the file - this metadata is used
    // when the device is acting as a USB drive, and unless it's explicitly
    // refreshed, it'll get out of step with the real file.
    const LocalRef<jstring> t (javaString (file.getFullPathName()));
    android.activity.callVoidMethod (BeastAppActivity.scanFile, t.get());
#endif

    return result;
}

//==============================================================================
String SystemStats::getEnvironmentVariable (const String& name, const String& defaultValue)
{
    if (const char* s = ::getenv (name.toUTF8()))
        return String::fromUTF8 (s);

    return defaultValue;
}

//==============================================================================
void MemoryMappedFile::openInternal (const File& file, AccessMode mode)
{
    bassert (mode == readOnly || mode == readWrite);

    if (range.getStart() > 0)
    {
        const long pageSize = sysconf (_SC_PAGE_SIZE);
        range.setStart (range.getStart() - (range.getStart() % pageSize));
    }

    fileHandle = open (file.getFullPathName().toUTF8(),
                       mode == readWrite ? (O_CREAT + O_RDWR) : O_RDONLY, 00644);

    if (fileHandle != -1)
    {
        void* m = mmap (0, (size_t) range.getLength(),
                        mode == readWrite ? (PROT_READ | PROT_WRITE) : PROT_READ,
                        MAP_SHARED, fileHandle,
                        (off_t) range.getStart());

        if (m != MAP_FAILED)
        {
            address = m;
            madvise (m, (size_t) range.getLength(), MADV_SEQUENTIAL);
        }
        else
        {
            range = Range<int64>();
        }
    }
}

MemoryMappedFile::~MemoryMappedFile()
{
    if (address != nullptr)
        munmap (address, (size_t) range.getLength());

    if (fileHandle != 0)
        close (fileHandle);
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

//==============================================================================
int64 File::getBytesFreeOnVolume() const
{
    struct statfs buf;
    if (beast_doStatFS (*this, buf))
        return (int64) buf.f_bsize * (int64) buf.f_bavail; // Note: this returns space available to non-super user

    return 0;
}

int64 File::getVolumeTotalSize() const
{
    struct statfs buf;
    if (beast_doStatFS (*this, buf))
        return (int64) buf.f_bsize * (int64) buf.f_blocks;

    return 0;
}

String File::getVolumeLabel() const
{
   #if BEAST_MAC
    struct VolAttrBuf
    {
        u_int32_t       length;
        attrreference_t mountPointRef;
        char            mountPointSpace [MAXPATHLEN];
    } attrBuf;

    struct attrlist attrList;
    zerostruct (attrList); // (can't use "= { 0 }" on this object because it's typedef'ed as a C struct)
    attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
    attrList.volattr = ATTR_VOL_INFO | ATTR_VOL_NAME;

    File f (*this);

    for (;;)
    {
        if (getattrlist (f.getFullPathName().toUTF8(), &attrList, &attrBuf, sizeof (attrBuf), 0) == 0)
            return String::fromUTF8 (((const char*) &attrBuf.mountPointRef) + attrBuf.mountPointRef.attr_dataoffset,
                                     (int) attrBuf.mountPointRef.attr_length);

        const File parent (f.getParentDirectory());

        if (f == parent)
            break;

        f = parent;
    }
   #endif

    return String::empty;
}

int File::getVolumeSerialNumber() const
{
    int result = 0;
/*    int fd = open (getFullPathName().toUTF8(), O_RDONLY | O_NONBLOCK);

    char info [512];

    #ifndef HDIO_GET_IDENTITY
     #define HDIO_GET_IDENTITY 0x030d
    #endif

    if (ioctl (fd, HDIO_GET_IDENTITY, info) == 0)
    {
        DBG (String (info + 20, 20));
        result = String (info + 20, 20).trim().getIntValue();
    }

    close (fd);*/
    return result;
}

//==============================================================================
void beast_runSystemCommand (const String&);
void beast_runSystemCommand (const String& command)
{
    int result = system (command.toUTF8());
    (void) result;
}

String beast_getOutputFromCommand (const String&);
String beast_getOutputFromCommand (const String& command)
{
    // slight bodge here, as we just pipe the output into a temp file and read it...
    const File tempFile (File::getSpecialLocation (File::tempDirectory)
                           .getNonexistentChildFile (String::toHexString (Random::getSystemRandom().nextInt()), ".tmp", false));

    beast_runSystemCommand (command + " > " + tempFile.getFullPathName());

    String result (tempFile.loadFileAsString());
    tempFile.deleteFile();
    return result;
}


//==============================================================================
#if BEAST_IOS
class InterProcessLock::Pimpl
{
public:
    Pimpl (const String&, int)
        : handle (1), refCount (1) // On iOS just fake success..
    {
    }

    int handle, refCount;
};

#else

class InterProcessLock::Pimpl
{
public:
    Pimpl (const String& lockName, const int timeOutMillisecs)
        : handle (0), refCount (1)
    {
       #if BEAST_MAC
        if (! createLockFile (File ("~/Library/Caches/com.beast.locks").getChildFile (lockName), timeOutMillisecs))
            // Fallback if the user's home folder is on a network drive with no ability to lock..
            createLockFile (File ("/tmp/com.beast.locks").getChildFile (lockName), timeOutMillisecs);

       #else
        File tempFolder ("/var/tmp");
        if (! tempFolder.isDirectory())
            tempFolder = "/tmp";

        createLockFile (tempFolder.getChildFile (lockName), timeOutMillisecs);
       #endif
    }

    ~Pimpl()
    {
        closeFile();
    }

    bool createLockFile (const File& file, const int timeOutMillisecs)
    {
        file.create();
        handle = open (file.getFullPathName().toUTF8(), O_RDWR);

        if (handle != 0)
        {
            struct flock fl;
            zerostruct (fl);

            fl.l_whence = SEEK_SET;
            fl.l_type = F_WRLCK;

            const int64 endTime = Time::currentTimeMillis() + timeOutMillisecs;

            for (;;)
            {
                const int result = fcntl (handle, F_SETLK, &fl);

                if (result >= 0)
                    return true;

                const int error = errno;

                if (error != EINTR)
                {
                    if (error == EBADF || error == ENOTSUP)
                        return false;

                    if (timeOutMillisecs == 0
                         || (timeOutMillisecs > 0 && Time::currentTimeMillis() >= endTime))
                        break;

                    Thread::sleep (10);
                }
            }
        }

        closeFile();
        return true; // only false if there's a file system error. Failure to lock still returns true.
    }

    void closeFile()
    {
        if (handle != 0)
        {
            struct flock fl;
            zerostruct (fl);

            fl.l_whence = SEEK_SET;
            fl.l_type = F_UNLCK;

            while (! (fcntl (handle, F_SETLKW, &fl) >= 0 || errno != EINTR))
            {}

            close (handle);
            handle = 0;
        }
    }

    int handle, refCount;
};
#endif

InterProcessLock::InterProcessLock (const String& nm)  : name (nm)
{
}

InterProcessLock::~InterProcessLock()
{
}

bool InterProcessLock::enter (const int timeOutMillisecs)
{
    const ScopedLock sl (lock);

    if (pimpl == nullptr)
    {
        pimpl = new Pimpl (name, timeOutMillisecs);

        if (pimpl->handle == 0)
            pimpl = nullptr;
    }
    else
    {
        pimpl->refCount++;
    }

    return pimpl != nullptr;
}

void InterProcessLock::exit()
{
    const ScopedLock sl (lock);

    // Trying to release the lock too many times!
    bassert (pimpl != nullptr);

    if (pimpl != nullptr && --(pimpl->refCount) == 0)
        pimpl = nullptr;
}

//==============================================================================

bool DynamicLibrary::open (const String& name)
{
    close();
    handle = dlopen (name.isEmpty() ? nullptr : name.toUTF8().getAddress(), RTLD_LOCAL | RTLD_NOW);
    return handle != nullptr;
}

void DynamicLibrary::close()
{
    if (handle != nullptr)
    {
        dlclose (handle);
        handle = nullptr;
    }
}

void* DynamicLibrary::getFunction (const String& functionName) noexcept
{
    return handle != nullptr ? dlsym (handle, functionName.toUTF8()) : nullptr;
}



//==============================================================================
class ChildProcess::ActiveProcess : LeakChecked <ActiveProcess>, public Uncopyable
{
public:
    ActiveProcess (const StringArray& arguments)
        : childPID (0), pipeHandle (0), readHandle (0)
    {
        int pipeHandles[2] = { 0 };

        if (pipe (pipeHandles) == 0)
        {
            const pid_t result = fork();

            if (result < 0)
            {
                close (pipeHandles[0]);
                close (pipeHandles[1]);
            }
            else if (result == 0)
            {
                // we're the child process..
                close (pipeHandles[0]);   // close the read handle
                dup2 (pipeHandles[1], 1); // turns the pipe into stdout
                dup2 (pipeHandles[1], 2); //  + stderr
                close (pipeHandles[1]);

                Array<char*> argv;
                for (int i = 0; i < arguments.size(); ++i)
                    if (arguments[i].isNotEmpty())
                        argv.add (arguments[i].toUTF8().getAddress());

                argv.add (nullptr);

                execvp (argv[0], argv.getRawDataPointer());
                exit (-1);
            }
            else
            {
                // we're the parent process..
                childPID = result;
                pipeHandle = pipeHandles[0];
                close (pipeHandles[1]); // close the write handle
            }
        }
    }

    ~ActiveProcess()
    {
        if (readHandle != 0)
            fclose (readHandle);

        if (pipeHandle != 0)
            close (pipeHandle);
    }

    bool isRunning() const
    {
        if (childPID != 0)
        {
            int childState;
            const int pid = waitpid (childPID, &childState, WNOHANG);
            return pid == 0 || ! (WIFEXITED (childState) || WIFSIGNALED (childState));
        }

        return false;
    }

    int read (void* const dest, const int numBytes)
    {
        bassert (dest != nullptr);

        #ifdef fdopen
         #error // the zlib headers define this function as NULL!
        #endif

        if (readHandle == 0 && childPID != 0)
            readHandle = fdopen (pipeHandle, "r");

        if (readHandle != 0)
            return (int) fread (dest, 1, (size_t) numBytes, readHandle);

        return 0;
    }

    bool killProcess() const
    {
        return ::kill (childPID, SIGKILL) == 0;
    }

    int childPID;

private:
    int pipeHandle;
    FILE* readHandle;
};

bool ChildProcess::start (const String& command)
{
    return start (StringArray::fromTokens (command, true));
}

bool ChildProcess::start (const StringArray& args)
{
    if (args.size() == 0)
        return false;

    activeProcess = new ActiveProcess (args);

    if (activeProcess->childPID == 0)
        activeProcess = nullptr;

    return activeProcess != nullptr;
}

bool ChildProcess::isRunning() const
{
    return activeProcess != nullptr && activeProcess->isRunning();
}

int ChildProcess::readProcessOutput (void* dest, int numBytes)
{
    return activeProcess != nullptr ? activeProcess->read (dest, numBytes) : 0;
}

bool ChildProcess::kill()
{
    return activeProcess == nullptr || activeProcess->killProcess();
}

//==============================================================================
struct HighResolutionTimer::Pimpl : public Uncopyable
{
    Pimpl (HighResolutionTimer& t)  : owner (t), thread (0), shouldStop (false)
    {
    }

    ~Pimpl()
    {
        bassert (thread == 0);
    }

    void start (int newPeriod)
    {
        periodMs = newPeriod;

        if (thread == 0)
        {
            shouldStop = false;

            if (pthread_create (&thread, nullptr, timerThread, this) == 0)
                setThreadToRealtime (thread, (uint64) newPeriod);
            else
                bassertfalse;
        }
    }

    void stop()
    {
        if (thread != 0)
        {
            shouldStop = true;

            while (thread != 0 && thread != pthread_self())
                Thread::yield();
        }
    }

    HighResolutionTimer& owner;
    int volatile periodMs;

private:
    pthread_t thread;
    bool volatile shouldStop;

    static void* timerThread (void* param)
    {
       #if ! BEAST_ANDROID
        int dummy;
        pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, &dummy);
       #endif

        reinterpret_cast<Pimpl*> (param)->timerThread();
        return nullptr;
    }

    void timerThread()
    {
        Clock clock (periodMs);

        while (! shouldStop)
        {
            clock.wait();
            owner.hiResTimerCallback();
        }

        periodMs = 0;
        thread = 0;
    }

    struct Clock
    {
       #if BEAST_MAC || BEAST_IOS
        Clock (double millis) noexcept
        {
            mach_timebase_info_data_t timebase;
            (void) mach_timebase_info (&timebase);
            delta = (((uint64_t) (millis * 1000000.0)) * timebase.numer) / timebase.denom;
            time = mach_absolute_time();
        }

        void wait() noexcept
        {
            time += delta;
            mach_wait_until (time);
        }

        uint64_t time, delta;

       #elif BEAST_ANDROID || BEAST_BSD
        Clock (double millis) noexcept  : delta ((uint64) (millis * 1000000))
        {
        }

        void wait() noexcept
        {
            struct timespec t;
            t.tv_sec  = (time_t) (delta / 1000000000);
            t.tv_nsec = (long)   (delta % 1000000000);
            nanosleep (&t, nullptr);
        }

        uint64 delta;
       #else
        Clock (double millis) noexcept  : delta ((uint64) (millis * 1000000))
        {
            struct timespec t;
            clock_gettime (CLOCK_MONOTONIC, &t);
            time = 1000000000 * (int64) t.tv_sec + t.tv_nsec;
        }

        void wait() noexcept
        {
            time += delta;

            struct timespec t;
            t.tv_sec  = (time_t) (time / 1000000000);
            t.tv_nsec = (long)   (time % 1000000000);
            clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr);
        }

        uint64 time, delta;
       #endif
    };

    static bool setThreadToRealtime (pthread_t thread, uint64 periodMs)
    {
       #if BEAST_MAC || BEAST_IOS
        thread_time_constraint_policy_data_t policy;
        policy.period      = (uint32_t) (periodMs * 1000000);
        policy.computation = 50000;
        policy.constraint  = policy.period;
        policy.preemptible = true;

        return thread_policy_set (pthread_mach_thread_np (thread),
                                  THREAD_TIME_CONSTRAINT_POLICY,
                                  (thread_policy_t) &policy,
                                  THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;

       #else
        (void) periodMs;
        struct sched_param param;
        param.sched_priority = sched_get_priority_max (SCHED_RR);
        return pthread_setschedparam (thread, SCHED_RR, &param) == 0;

       #endif
    }
};
