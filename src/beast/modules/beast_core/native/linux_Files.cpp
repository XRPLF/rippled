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

enum
{
    U_ISOFS_SUPER_MAGIC = 0x9660,   // linux/iso_fs.h
    U_MSDOS_SUPER_MAGIC = 0x4d44,   // linux/msdos_fs.h
    U_NFS_SUPER_MAGIC = 0x6969,     // linux/nfs_fs.h
    U_SMB_SUPER_MAGIC = 0x517B      // linux/smb_fs.h
};

//==============================================================================
bool File::copyInternal (const File& dest) const
{
    FileInputStream in (*this);

    if (dest.deleteFile())
    {
        {
            FileOutputStream out (dest);

            if (out.failedToOpen())
                return false;

            if (out.writeFromInputStream (in, -1) == getSize())
                return true;
        }

        dest.deleteFile();
    }

    return false;
}

void File::findFileSystemRoots (Array<File>& destArray)
{
    destArray.add (File ("/"));
}

//==============================================================================
bool File::isOnCDRomDrive() const
{
    struct statfs buf;

    return statfs (getFullPathName().toUTF8(), &buf) == 0
             && buf.f_type == (short) U_ISOFS_SUPER_MAGIC;
}

bool File::isOnHardDisk() const
{
    struct statfs buf;

    if (statfs (getFullPathName().toUTF8(), &buf) == 0)
    {
        switch (buf.f_type)
        {
            case U_ISOFS_SUPER_MAGIC:   // CD-ROM
            case U_MSDOS_SUPER_MAGIC:   // Probably floppy (but could be mounted FAT filesystem)
            case U_NFS_SUPER_MAGIC:     // Network NFS
            case U_SMB_SUPER_MAGIC:     // Network Samba
                return false;

            default:
                // Assume anything else is a hard-disk (but note it could
                // be a RAM disk.  There isn't a good way of determining
                // this for sure)
                return true;
        }
    }

    // Assume so if this fails for some reason
    return true;
}

bool File::isOnRemovableDrive() const
{
    bassertfalse; // xxx not implemented for linux!
    return false;
}

bool File::isHidden() const
{
    return getFileName().startsWithChar ('.');
}

//==============================================================================
namespace
{
    File beast_readlink (const String& file, const File& defaultFile)
    {
        const size_t size = 8192;
        HeapBlock<char> buffer;
        buffer.malloc (size + 4);

        const size_t numBytes = readlink (file.toUTF8(), buffer, size);

        if (numBytes > 0 && numBytes <= size)
            return File (file).getSiblingFile (String::fromUTF8 (buffer, (int) numBytes));

        return defaultFile;
    }
}

File File::getLinkedTarget() const
{
    return beast_readlink (getFullPathName().toUTF8(), *this);
}

//==============================================================================
static File resolveXDGFolder (const char* const type, const char* const fallbackFolder)
{
    StringArray confLines;
    File ("~/.config/user-dirs.dirs").readLines (confLines);

    for (int i = 0; i < confLines.size(); ++i)
    {
        const String line (confLines[i].trimStart());

        if (line.startsWith (type))
        {
            // eg. resolve XDG_MUSIC_DIR="$HOME/Music" to /home/user/Music
            const File f (line.replace ("$HOME", File ("~").getFullPathName())
                              .fromFirstOccurrenceOf ("=", false, false)
                              .trim().unquoted());

            if (f.isDirectory())
                return f;
        }
    }

    return File (fallbackFolder);
}

const char* const* beast_argv = nullptr;
int beast_argc = 0;

File File::getSpecialLocation (const SpecialLocationType type)
{
    switch (type)
    {
        case userHomeDirectory:
        {
            const char* homeDir = getenv ("HOME");

            if (const char* homeDir = getenv ("HOME"))
                return File (CharPointer_UTF8 (homeDir));

            if (struct passwd* const pw = getpwuid (getuid()))
                return File (CharPointer_UTF8 (pw->pw_dir));
            
            return File (CharPointer_UTF8 (homeDir));
        }

        case userDocumentsDirectory:          return resolveXDGFolder ("XDG_DOCUMENTS_DIR", "~");
        case userMusicDirectory:              return resolveXDGFolder ("XDG_MUSIC_DIR",     "~");
        case userMoviesDirectory:             return resolveXDGFolder ("XDG_VIDEOS_DIR",    "~");
        case userPicturesDirectory:           return resolveXDGFolder ("XDG_PICTURES_DIR",  "~");
        case userDesktopDirectory:            return resolveXDGFolder ("XDG_DESKTOP_DIR",   "~/Desktop");
        case userApplicationDataDirectory:    return File ("~");
        case commonDocumentsDirectory:
        case commonApplicationDataDirectory:  return File ("/var");
        case globalApplicationsDirectory:     return File ("/usr");

        case tempDirectory:
        {
            File tmp ("/var/tmp");

            if (! tmp.isDirectory())
            {
                tmp = "/tmp";

                if (! tmp.isDirectory())
                    tmp = File::getCurrentWorkingDirectory();
            }

            return tmp;
        }

        case invokedExecutableFile:
            if (beast_argv != nullptr && beast_argc > 0)
                return File (CharPointer_UTF8 (beast_argv[0]));
            // deliberate fall-through...

        case currentExecutableFile:
        case currentApplicationFile:
            return beast_getExecutableFile();

        case hostApplicationPath:
            return beast_readlink ("/proc/self/exe", beast_getExecutableFile());

        default:
            bassertfalse; // unknown type?
            break;
    }

    return File::nonexistent ();
}

//==============================================================================
String File::getVersion() const
{
    return String::empty; // xxx not yet implemented
}

//==============================================================================
bool File::moveToTrash() const
{
    if (! exists())
        return true;

    File trashCan ("~/.Trash");

    if (! trashCan.isDirectory())
        trashCan = "~/.local/share/Trash/files";

    if (! trashCan.isDirectory())
        return false;

    return moveFileTo (trashCan.getNonexistentChildFile (getFileNameWithoutExtension(),
                                                         getFileExtension()));
}

//==============================================================================
class DirectoryIterator::NativeIterator::Pimpl : public Uncopyable
{
public:
    Pimpl (const File& directory, const String& wildCard_)
        : parentDir (File::addTrailingSeparator (directory.getFullPathName())),
          wildCard (wildCard_),
          dir (opendir (directory.getFullPathName().toUTF8()))
    {
    }

    ~Pimpl()
    {
        if (dir != nullptr)
            closedir (dir);
    }

    bool next (String& filenameFound,
               bool* const isDir, bool* const isHidden, int64* const fileSize,
               Time* const modTime, Time* const creationTime, bool* const isReadOnly)
    {
        if (dir != nullptr)
        {
            const char* wildcardUTF8 = nullptr;

            for (;;)
            {
                struct dirent* const de = readdir (dir);

                if (de == nullptr)
                    break;

                if (wildcardUTF8 == nullptr)
                    wildcardUTF8 = wildCard.toUTF8();

                if (fnmatch (wildcardUTF8, de->d_name, FNM_CASEFOLD) == 0)
                {
                    filenameFound = CharPointer_UTF8 (de->d_name);

                    updateStatInfoForFile (parentDir + filenameFound, isDir, fileSize, modTime, creationTime, isReadOnly);

                    if (isHidden != nullptr)
                        *isHidden = filenameFound.startsWithChar ('.');

                    return true;
                }
            }
        }

        return false;
    }

private:
    String parentDir, wildCard;
    DIR* dir;
};

DirectoryIterator::NativeIterator::NativeIterator (const File& directory, const String& wildCard)
    : pimpl (new DirectoryIterator::NativeIterator::Pimpl (directory, wildCard))
{
}

DirectoryIterator::NativeIterator::~NativeIterator()
{
}

bool DirectoryIterator::NativeIterator::next (String& filenameFound,
                                              bool* const isDir, bool* const isHidden, int64* const fileSize,
                                              Time* const modTime, Time* const creationTime, bool* const isReadOnly)
{
    return pimpl->next (filenameFound, isDir, isHidden, fileSize, modTime, creationTime, isReadOnly);
}


//==============================================================================
static bool isFileExecutable (const String& filename)
{
    beast_statStruct info;

    return beast_stat (filename, info)
            && S_ISREG (info.st_mode)
            && access (filename.toUTF8(), X_OK) == 0;
}

bool Process::openDocument (const String& fileName, const String& parameters)
{
    String cmdString (fileName.replace (" ", "\\ ",false));
    cmdString << " " << parameters;

    if (    cmdString.startsWithIgnoreCase ("file:")
         || File::createFileWithoutCheckingPath (fileName).isDirectory()
         || ! isFileExecutable (fileName))
    {
        // create a command that tries to launch a bunch of likely browsers
        const char* const browserNames[] = { "xdg-open", "/etc/alternatives/x-www-browser", "firefox", "mozilla",
                                             "google-chrome", "chromium-browser", "opera", "konqueror" };
        StringArray cmdLines;

        for (int i = 0; i < numElementsInArray (browserNames); ++i)
            cmdLines.add (String (browserNames[i]) + " " + cmdString.trim().quoted());

        cmdString = cmdLines.joinIntoString (" || ");
    }

    const char* const argv[4] = { "/bin/sh", "-c", cmdString.toUTF8(), 0 };

    const int cpid = fork();

    if (cpid == 0)
    {
        setsid();

        // Child process
        execve (argv[0], (char**) argv, environ);
        exit (0);
    }

    return cpid >= 0;
}

void File::revealToUser() const
{
    if (isDirectory())
        startAsProcess();
    else if (getParentDirectory().exists())
        getParentDirectory().startAsProcess();
}
