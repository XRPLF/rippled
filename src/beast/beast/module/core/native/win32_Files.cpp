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

namespace beast
{

#ifndef INVALID_FILE_ATTRIBUTES
 #define INVALID_FILE_ATTRIBUTES ((DWORD) -1)
#endif

//==============================================================================
namespace WindowsFileHelpers
{
    DWORD getAtts (const String& path)
    {
        return GetFileAttributes (path.toWideCharPointer());
    }

    std::int64_t fileTimeToTime (const FILETIME* const ft)
    {
        static_bassert (sizeof (ULARGE_INTEGER) == sizeof (FILETIME)); // tell me if this fails!

        return (std::int64_t) ((reinterpret_cast<const ULARGE_INTEGER*> (ft)->QuadPart - 116444736000000000LL) / 10000);
    }

    FILETIME* timeToFileTime (const std::int64_t time, FILETIME* const ft) noexcept
    {
        if (time <= 0)
            return nullptr;

        reinterpret_cast<ULARGE_INTEGER*> (ft)->QuadPart = (ULONGLONG) (time * 10000 + 116444736000000000LL);
        return ft;
    }

    String getDriveFromPath (String path)
    {
        if (path.isNotEmpty() && path[1] == ':' && path[2] == 0)
            path << '\\';

        const size_t numBytes = CharPointer_UTF16::getBytesRequiredFor (path.getCharPointer()) + 4;
        HeapBlock<WCHAR> pathCopy;
        pathCopy.calloc (numBytes, 1);
        path.copyToUTF16 (pathCopy, numBytes);

        if (PathStripToRoot (pathCopy))
            path = static_cast <const WCHAR*> (pathCopy);

        return path;
    }

    std::int64_t getDiskSpaceInfo (const String& path, const bool total)
    {
        ULARGE_INTEGER spc, tot, totFree;

        if (GetDiskFreeSpaceEx (getDriveFromPath (path).toWideCharPointer(), &spc, &tot, &totFree))
            return total ? (std::int64_t) tot.QuadPart
                         : (std::int64_t) spc.QuadPart;

        return 0;
    }

    unsigned int getWindowsDriveType (const String& path)
    {
        return GetDriveType (getDriveFromPath (path).toWideCharPointer());
    }

    File getSpecialFolderPath (int type)
    {
        WCHAR path [MAX_PATH + 256];

        if (SHGetSpecialFolderPath (0, path, type, FALSE))
            return File (String (path));

        return File::nonexistent ();
    }

    File getModuleFileName (HINSTANCE moduleHandle)
    {
        WCHAR dest [MAX_PATH + 256];
        dest[0] = 0;
        GetModuleFileName (moduleHandle, dest, (DWORD) numElementsInArray (dest));
        return File (String (dest));
    }

    Result getResultForLastError()
    {
        TCHAR messageBuffer [256] = { 0 };

        FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, GetLastError(), MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
                       messageBuffer, (DWORD) numElementsInArray (messageBuffer) - 1, nullptr);

        return Result::fail (String (messageBuffer));
    }
}

//==============================================================================
const beast_wchar File::separator = '\\';
const String File::separatorString ("\\");


//==============================================================================
bool File::exists() const
{
    return fullPath.isNotEmpty()
            && WindowsFileHelpers::getAtts (fullPath) != INVALID_FILE_ATTRIBUTES;
}

bool File::existsAsFile() const
{
    return fullPath.isNotEmpty()
            && (WindowsFileHelpers::getAtts (fullPath) & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool File::isDirectory() const
{
    const DWORD attr = WindowsFileHelpers::getAtts (fullPath);
    return ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) && (attr != INVALID_FILE_ATTRIBUTES);
}

bool File::hasWriteAccess() const
{
    if (exists())
        return (WindowsFileHelpers::getAtts (fullPath) & FILE_ATTRIBUTE_READONLY) == 0;

    // on windows, it seems that even read-only directories can still be written into,
    // so checking the parent directory's permissions would return the wrong result..
    return true;
}

bool File::setFileReadOnlyInternal (const bool shouldBeReadOnly) const
{
    const DWORD oldAtts = WindowsFileHelpers::getAtts (fullPath);

    if (oldAtts == INVALID_FILE_ATTRIBUTES)
        return false;

    const DWORD newAtts = shouldBeReadOnly ? (oldAtts |  FILE_ATTRIBUTE_READONLY)
                                           : (oldAtts & ~FILE_ATTRIBUTE_READONLY);
    return newAtts == oldAtts
            || SetFileAttributes (fullPath.toWideCharPointer(), newAtts) != FALSE;
}

bool File::isHidden() const
{
    return (WindowsFileHelpers::getAtts (fullPath) & FILE_ATTRIBUTE_HIDDEN) != 0;
}

//==============================================================================
bool File::deleteFile() const
{
    if (! exists())
        return true;

    return isDirectory() ? RemoveDirectory (fullPath.toWideCharPointer()) != 0
                         : DeleteFile (fullPath.toWideCharPointer()) != 0;
}

bool File::moveToTrash() const
{
    if (! exists())
        return true;

    // The string we pass in must be double null terminated..
    const size_t numBytes = CharPointer_UTF16::getBytesRequiredFor (fullPath.getCharPointer()) + 8;
    HeapBlock<WCHAR> doubleNullTermPath;
    doubleNullTermPath.calloc (numBytes, 1);
    fullPath.copyToUTF16 (doubleNullTermPath, numBytes);

    SHFILEOPSTRUCT fos = { 0 };
    fos.wFunc = FO_DELETE;
    fos.pFrom = doubleNullTermPath;
    fos.fFlags = FOF_ALLOWUNDO | FOF_NOERRORUI | FOF_SILENT | FOF_NOCONFIRMATION
                   | FOF_NOCONFIRMMKDIR | FOF_RENAMEONCOLLISION;

    return SHFileOperation (&fos) == 0;
}

bool File::copyInternal (const File& dest) const
{
    return CopyFile (fullPath.toWideCharPointer(), dest.getFullPathName().toWideCharPointer(), false) != 0;
}

bool File::moveInternal (const File& dest) const
{
    return MoveFile (fullPath.toWideCharPointer(), dest.getFullPathName().toWideCharPointer()) != 0;
}

Result File::createDirectoryInternal (const String& fileName) const
{
    return CreateDirectory (fileName.toWideCharPointer(), 0) ? Result::ok()
                                                             : WindowsFileHelpers::getResultForLastError();
}

//==============================================================================
std::int64_t beast_fileSetPosition (void* handle, std::int64_t pos)
{
    LARGE_INTEGER li;
    li.QuadPart = pos;
    li.LowPart = SetFilePointer ((HANDLE) handle, (LONG) li.LowPart, &li.HighPart, FILE_BEGIN);  // (returns -1 if it fails)
    return li.QuadPart;
}

void FileInputStream::openHandle()
{
    HANDLE h = CreateFile (file.getFullPathName().toWideCharPointer(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, 0);

    if (h != INVALID_HANDLE_VALUE)
        fileHandle = (void*) h;
    else
        status = WindowsFileHelpers::getResultForLastError();
}

void FileInputStream::closeHandle()
{
    CloseHandle ((HANDLE) fileHandle);
}

size_t FileInputStream::readInternal (void* buffer, size_t numBytes)
{
    if (fileHandle != 0)
    {
        DWORD actualNum = 0;
        if (! ReadFile ((HANDLE) fileHandle, buffer, (DWORD) numBytes, &actualNum, 0))
            status = WindowsFileHelpers::getResultForLastError();

        return (size_t) actualNum;
    }

    return 0;
}

//==============================================================================
void FileOutputStream::openHandle()
{
    HANDLE h = CreateFile (file.getFullPathName().toWideCharPointer(), GENERIC_WRITE, FILE_SHARE_READ, 0,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    if (h != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER li;
        li.QuadPart = 0;
        li.LowPart = SetFilePointer (h, 0, &li.HighPart, FILE_END);

        if (li.LowPart != INVALID_SET_FILE_POINTER)
        {
            fileHandle = (void*) h;
            currentPosition = li.QuadPart;
            return;
        }
    }

    status = WindowsFileHelpers::getResultForLastError();
}

void FileOutputStream::closeHandle()
{
    CloseHandle ((HANDLE) fileHandle);
}

std::ptrdiff_t FileOutputStream::writeInternal (const void* buffer, size_t numBytes)
{
    if (fileHandle != nullptr)
    {
        DWORD actualNum = 0;
        if (! WriteFile ((HANDLE) fileHandle, buffer, (DWORD) numBytes, &actualNum, 0))
            status = WindowsFileHelpers::getResultForLastError();

        return (std::ptrdiff_t) actualNum;
    }

    return 0;
}

void FileOutputStream::flushInternal()
{
    if (fileHandle != nullptr)
        if (! FlushFileBuffers ((HANDLE) fileHandle))
            status = WindowsFileHelpers::getResultForLastError();
}

Result FileOutputStream::truncate()
{
    if (fileHandle == nullptr)
        return status;

    flush();
    return SetEndOfFile ((HANDLE) fileHandle) ? Result::ok()
                                              : WindowsFileHelpers::getResultForLastError();
}

//==============================================================================

Result RandomAccessFile::nativeOpen (File const& path, Mode mode)
{
    bassert (! isOpen ());

    Result result (Result::ok ());

    DWORD dwDesiredAccess;
    switch (mode)
    {
    case readOnly:
        dwDesiredAccess = GENERIC_READ;
        break;

    default:
    case readWrite:
        dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
        break;   
    };

    DWORD dwCreationDisposition;
    switch (mode)
    {
    case readOnly:
        dwCreationDisposition = OPEN_EXISTING;
        break;

    default:
    case readWrite:
         dwCreationDisposition = OPEN_ALWAYS;
         break;
    };

    HANDLE h = CreateFile (path.getFullPathName().toWideCharPointer(),
                           dwDesiredAccess,
                           FILE_SHARE_READ,
                           0,
                           dwCreationDisposition,
                           FILE_ATTRIBUTE_NORMAL,
                           0);

    if (h != INVALID_HANDLE_VALUE)
    {
        file = path;
        fileHandle = h;

        result = setPosition (0);

        if (result.failed ())
            nativeClose ();
    }
    else
    {
        result = WindowsFileHelpers::getResultForLastError();
    }

    return result;
}

void RandomAccessFile::nativeClose ()
{
    bassert (isOpen ());

    CloseHandle ((HANDLE) fileHandle);

    file = File::nonexistent ();
    fileHandle = nullptr;
    currentPosition = 0;
}

Result RandomAccessFile::nativeSetPosition (FileOffset newPosition)
{
    bassert (isOpen ());

    Result result (Result::ok ());

    LARGE_INTEGER li;
    li.QuadPart = newPosition;
    li.LowPart = SetFilePointer ((HANDLE) fileHandle,
                                 (LONG) li.LowPart,
                                 &li.HighPart,
                                 FILE_BEGIN);

    if (li.LowPart != INVALID_SET_FILE_POINTER)
    {
        currentPosition = li.QuadPart;
    }
    else
    {
        result = WindowsFileHelpers::getResultForLastError();
    }

    return result;
}

Result RandomAccessFile::nativeRead (void* buffer, ByteCount numBytes, ByteCount* pActualAmount)
{
    bassert (isOpen ());

    Result result (Result::ok ());

    DWORD actualNum = 0;

    if (! ReadFile ((HANDLE) fileHandle, buffer, (DWORD) numBytes, &actualNum, 0))
        result = WindowsFileHelpers::getResultForLastError();

    currentPosition += actualNum;

    if (pActualAmount != nullptr)
        *pActualAmount = actualNum;

    return result;
}

Result RandomAccessFile::nativeWrite (void const* data, ByteCount numBytes, size_t* pActualAmount)
{
    bassert (isOpen ());

    Result result (Result::ok ());

    DWORD actualNum = 0;

    if (! WriteFile ((HANDLE) fileHandle, data, (DWORD) numBytes, &actualNum, 0))
        result = WindowsFileHelpers::getResultForLastError();

    if (pActualAmount != nullptr)
        *pActualAmount = actualNum;

    return result;
}

Result RandomAccessFile::nativeTruncate ()
{
    bassert (isOpen ());

    Result result (Result::ok ());

    if (! SetEndOfFile ((HANDLE) fileHandle))
        result = WindowsFileHelpers::getResultForLastError();

    return result;
}

Result RandomAccessFile::nativeFlush ()
{
    bassert (isOpen ());

    Result result (Result::ok ());

    if (! FlushFileBuffers ((HANDLE) fileHandle))
        result = WindowsFileHelpers::getResultForLastError();

    return result;
}

//==============================================================================

std::int64_t File::getSize() const
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (GetFileAttributesEx (fullPath.toWideCharPointer(), GetFileExInfoStandard, &attributes))
        return (((std::int64_t) attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;

    return 0;
}

void File::getFileTimesInternal (std::int64_t& modificationTime, std::int64_t& accessTime, std::int64_t& creationTime) const
{
    using namespace WindowsFileHelpers;
    WIN32_FILE_ATTRIBUTE_DATA attributes;

    if (GetFileAttributesEx (fullPath.toWideCharPointer(), GetFileExInfoStandard, &attributes))
    {
        modificationTime = fileTimeToTime (&attributes.ftLastWriteTime);
        creationTime     = fileTimeToTime (&attributes.ftCreationTime);
        accessTime       = fileTimeToTime (&attributes.ftLastAccessTime);
    }
    else
    {
        creationTime = accessTime = modificationTime = 0;
    }
}

bool File::setFileTimesInternal (std::int64_t modificationTime, std::int64_t accessTime, std::int64_t creationTime) const
{
    using namespace WindowsFileHelpers;

    bool ok = false;
    HANDLE h = CreateFile (fullPath.toWideCharPointer(), GENERIC_WRITE, FILE_SHARE_READ, 0,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    if (h != INVALID_HANDLE_VALUE)
    {
        FILETIME m, a, c;

        ok = SetFileTime (h,
                          timeToFileTime (creationTime, &c),
                          timeToFileTime (accessTime, &a),
                          timeToFileTime (modificationTime, &m)) != 0;

        CloseHandle (h);
    }

    return ok;
}

//==============================================================================
void File::findFileSystemRoots (Array<File>& destArray)
{
    TCHAR buffer [2048] = { 0 };
    GetLogicalDriveStrings (2048, buffer);

    const TCHAR* n = buffer;
    StringArray roots;

    while (*n != 0)
    {
        roots.add (String (n));

        while (*n++ != 0)
        {}
    }

    roots.sort (true);

    for (int i = 0; i < roots.size(); ++i)
        destArray.add (roots [i]);
}

//==============================================================================
String File::getVolumeLabel() const
{
    TCHAR dest[64];
    if (! GetVolumeInformation (WindowsFileHelpers::getDriveFromPath (getFullPathName()).toWideCharPointer(), dest,
                                (DWORD) numElementsInArray (dest), 0, 0, 0, 0, 0))
        dest[0] = 0;

    return dest;
}

int File::getVolumeSerialNumber() const
{
    TCHAR dest[64];
    DWORD serialNum;

    if (! GetVolumeInformation (WindowsFileHelpers::getDriveFromPath (getFullPathName()).toWideCharPointer(), dest,
                                (DWORD) numElementsInArray (dest), &serialNum, 0, 0, 0, 0))
        return 0;

    return (int) serialNum;
}

std::int64_t File::getBytesFreeOnVolume() const
{
    return WindowsFileHelpers::getDiskSpaceInfo (getFullPathName(), false);
}

std::int64_t File::getVolumeTotalSize() const
{
    return WindowsFileHelpers::getDiskSpaceInfo (getFullPathName(), true);
}

//==============================================================================
bool File::isOnCDRomDrive() const
{
    return WindowsFileHelpers::getWindowsDriveType (getFullPathName()) == DRIVE_CDROM;
}

bool File::isOnHardDisk() const
{
    if (fullPath.isEmpty())
        return false;

    const unsigned int n = WindowsFileHelpers::getWindowsDriveType (getFullPathName());

    if (fullPath.toLowerCase()[0] <= 'b' && fullPath[1] == ':')
        return n != DRIVE_REMOVABLE;

    return n != DRIVE_CDROM && n != DRIVE_REMOTE;
}

bool File::isOnRemovableDrive() const
{
    if (fullPath.isEmpty())
        return false;

    const unsigned int n = WindowsFileHelpers::getWindowsDriveType (getFullPathName());

    return n == DRIVE_CDROM
        || n == DRIVE_REMOTE
        || n == DRIVE_REMOVABLE
        || n == DRIVE_RAMDISK;
}

//==============================================================================
File File::getSpecialLocation (const SpecialLocationType type)
{
    int csidlType = 0;

    switch (type)
    {
        case userHomeDirectory:                 csidlType = CSIDL_PROFILE; break;
        case userDocumentsDirectory:            csidlType = CSIDL_PERSONAL; break;
        case userDesktopDirectory:              csidlType = CSIDL_DESKTOP; break;
        case userApplicationDataDirectory:      csidlType = CSIDL_APPDATA; break;
        case commonApplicationDataDirectory:    csidlType = CSIDL_COMMON_APPDATA; break;
        case commonDocumentsDirectory:          csidlType = CSIDL_COMMON_DOCUMENTS; break;
        case globalApplicationsDirectory:       csidlType = CSIDL_PROGRAM_FILES; break;
        case userMusicDirectory:                csidlType = 0x0d; /*CSIDL_MYMUSIC*/ break;
        case userMoviesDirectory:               csidlType = 0x0e; /*CSIDL_MYVIDEO*/ break;
        case userPicturesDirectory:             csidlType = 0x27; /*CSIDL_MYPICTURES*/ break;

        case tempDirectory:
        {
            WCHAR dest [2048];
            dest[0] = 0;
            GetTempPath ((DWORD) numElementsInArray (dest), dest);
            return File (String (dest));
        }

        case invokedExecutableFile:
        case currentExecutableFile:
        case currentApplicationFile:
            return WindowsFileHelpers::getModuleFileName ((HINSTANCE) Process::getCurrentModuleInstanceHandle());

        case hostApplicationPath:
            return WindowsFileHelpers::getModuleFileName (0);

        default:
            bassertfalse; // unknown type?
            return File::nonexistent ();
    }

    return WindowsFileHelpers::getSpecialFolderPath (csidlType);
}

//==============================================================================
File File::getCurrentWorkingDirectory()
{
    WCHAR dest [MAX_PATH + 256];
    dest[0] = 0;
    GetCurrentDirectory ((DWORD) numElementsInArray (dest), dest);
    return File (String (dest));
}

bool File::setAsCurrentWorkingDirectory() const
{
    return SetCurrentDirectory (getFullPathName().toWideCharPointer()) != FALSE;
}

//==============================================================================
String File::getVersion() const
{
    String result;

    DWORD handle = 0;
    DWORD bufferSize = GetFileVersionInfoSize (getFullPathName().toWideCharPointer(), &handle);
    HeapBlock<char> buffer;
    buffer.calloc (bufferSize);

    if (GetFileVersionInfo (getFullPathName().toWideCharPointer(), 0, bufferSize, buffer))
    {
        VS_FIXEDFILEINFO* vffi;
        UINT len = 0;

        if (VerQueryValue (buffer, (LPTSTR) _T("\\"), (LPVOID*) &vffi, &len))
        {
            result << (int) HIWORD (vffi->dwFileVersionMS) << '.'
                   << (int) LOWORD (vffi->dwFileVersionMS) << '.'
                   << (int) HIWORD (vffi->dwFileVersionLS) << '.'
                   << (int) LOWORD (vffi->dwFileVersionLS);
        }
    }

    return result;
}

//==============================================================================
File File::getLinkedTarget() const
{
    File result (*this);
    String p (getFullPathName());

    if (! exists())
        p += ".lnk";
    else if (! hasFileExtension (".lnk"))
        return result;

    ComSmartPtr <IShellLink> shellLink;
    ComSmartPtr <IPersistFile> persistFile;

    if (SUCCEEDED (shellLink.CoCreateInstance (CLSID_ShellLink))
         && SUCCEEDED (shellLink.QueryInterface (persistFile))
         && SUCCEEDED (persistFile->Load (p.toWideCharPointer(), STGM_READ))
         && SUCCEEDED (shellLink->Resolve (0, SLR_ANY_MATCH | SLR_NO_UI)))
    {
        WIN32_FIND_DATA winFindData;
        WCHAR resolvedPath [MAX_PATH];

        if (SUCCEEDED (shellLink->GetPath (resolvedPath, MAX_PATH, &winFindData, SLGP_UNCPRIORITY)))
            result = File (resolvedPath);
    }

    return result;
}

bool File::createLink (const String& description, const File& linkFileToCreate) const
{
    linkFileToCreate.deleteFile();

    ComSmartPtr <IShellLink> shellLink;
    ComSmartPtr <IPersistFile> persistFile;

    return SUCCEEDED (shellLink.CoCreateInstance (CLSID_ShellLink))
        && SUCCEEDED (shellLink->SetPath (getFullPathName().toWideCharPointer()))
        && SUCCEEDED (shellLink->SetDescription (description.toWideCharPointer()))
        && SUCCEEDED (shellLink.QueryInterface (persistFile))
        && SUCCEEDED (persistFile->Save (linkFileToCreate.getFullPathName().toWideCharPointer(), TRUE));
}

//==============================================================================
class DirectoryIterator::NativeIterator::Pimpl
    : LeakChecked <DirectoryIterator::NativeIterator::Pimpl>
    , public Uncopyable
{
public:
    Pimpl (const File& directory, const String& wildCard)
        : directoryWithWildCard (File::addTrailingSeparator (directory.getFullPathName()) + wildCard),
          handle (INVALID_HANDLE_VALUE)
    {
    }

    ~Pimpl()
    {
        if (handle != INVALID_HANDLE_VALUE)
            FindClose (handle);
    }

    bool next (String& filenameFound,
               bool* const isDir, bool* const isHidden, std::int64_t* const fileSize,
               Time* const modTime, Time* const creationTime, bool* const isReadOnly)
    {
        using namespace WindowsFileHelpers;
        WIN32_FIND_DATA findData;

        if (handle == INVALID_HANDLE_VALUE)
        {
            handle = FindFirstFile (directoryWithWildCard.toWideCharPointer(), &findData);

            if (handle == INVALID_HANDLE_VALUE)
                return false;
        }
        else
        {
            if (FindNextFile (handle, &findData) == 0)
                return false;
        }

        filenameFound = findData.cFileName;

        if (isDir != nullptr)         *isDir        = ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        if (isHidden != nullptr)      *isHidden     = ((findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0);
        if (isReadOnly != nullptr)    *isReadOnly   = ((findData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0);
        if (fileSize != nullptr)      *fileSize     = findData.nFileSizeLow + (((std::int64_t) findData.nFileSizeHigh) << 32);
        if (modTime != nullptr)       *modTime      = Time (fileTimeToTime (&findData.ftLastWriteTime));
        if (creationTime != nullptr)  *creationTime = Time (fileTimeToTime (&findData.ftCreationTime));

        return true;
    }

private:
    const String directoryWithWildCard;
    HANDLE handle;
};

DirectoryIterator::NativeIterator::NativeIterator (const File& directory, const String& wildCard)
    : pimpl (new DirectoryIterator::NativeIterator::Pimpl (directory, wildCard))
{
}

DirectoryIterator::NativeIterator::~NativeIterator()
{
}

bool DirectoryIterator::NativeIterator::next (String& filenameFound,
                                              bool* const isDir, bool* const isHidden, std::int64_t* const fileSize,
                                              Time* const modTime, Time* const creationTime, bool* const isReadOnly)
{
    return pimpl->next (filenameFound, isDir, isHidden, fileSize, modTime, creationTime, isReadOnly);
}


//==============================================================================
bool Process::openDocument (const String& fileName, const String& parameters)
{
    HINSTANCE hInstance = 0;

    hInstance = ShellExecute (0, 0, fileName.toWideCharPointer(),
                                parameters.toWideCharPointer(), 0, SW_SHOWDEFAULT);

    return hInstance > (HINSTANCE) 32;
}

void File::revealToUser() const
{
    DynamicLibrary dll ("Shell32.dll");
    BEAST_LOAD_WINAPI_FUNCTION (dll, ILCreateFromPathW, ilCreateFromPathW, ITEMIDLIST*, (LPCWSTR))
    BEAST_LOAD_WINAPI_FUNCTION (dll, ILFree, ilFree, void, (ITEMIDLIST*))
    BEAST_LOAD_WINAPI_FUNCTION (dll, SHOpenFolderAndSelectItems, shOpenFolderAndSelectItems, HRESULT, (ITEMIDLIST*, UINT, void*, DWORD))

    if (ilCreateFromPathW != nullptr && shOpenFolderAndSelectItems != nullptr && ilFree != nullptr)
    {
        if (ITEMIDLIST* const itemIDList = ilCreateFromPathW (fullPath.toWideCharPointer()))
        {
            shOpenFolderAndSelectItems (itemIDList, 0, nullptr, 0);
            ilFree (itemIDList);
        }
    }
}

} // beast
