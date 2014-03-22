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

/*
    Note that a lot of methods that you'd expect to find in this file actually
    live in beast_posix_SharedCode.h!
*/

//==============================================================================
bool File::copyInternal (const File& dest) const
{
    BEAST_AUTORELEASEPOOL
    {
        NSFileManager* fm = [NSFileManager defaultManager];

        return [fm fileExistsAtPath: beastStringToNS (fullPath)]
               #if defined (MAC_OS_X_VERSION_10_6) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6
                && [fm copyItemAtPath: beastStringToNS (fullPath)
                               toPath: beastStringToNS (dest.getFullPathName())
                                error: nil];
               #else
                && [fm copyPath: beastStringToNS (fullPath)
                         toPath: beastStringToNS (dest.getFullPathName())
                        handler: nil];
               #endif
    }
}

void File::findFileSystemRoots (Array<File>& destArray)
{
    destArray.add (File ("/"));
}


//==============================================================================
namespace FileHelpers
{
    static bool isFileOnDriveType (const File& f, const char* const* types)
    {
        struct statfs buf;

        if (beast_doStatFS (f, buf))
        {
            const String type (buf.f_fstypename);

            while (*types != 0)
                if (type.equalsIgnoreCase (*types++))
                    return true;
        }

        return false;
    }

    static bool isHiddenFile (const String& path)
    {
       #if defined (MAC_OS_X_VERSION_10_6) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_6
        BEAST_AUTORELEASEPOOL
        {
            NSNumber* hidden = nil;
            NSError* err = nil;

            return [[NSURL fileURLWithPath: beastStringToNS (path)]
                        getResourceValue: &hidden forKey: NSURLIsHiddenKey error: &err]
                    && [hidden boolValue];
        }
       #elif BEAST_IOS
        return File (path).getFileName().startsWithChar ('.');
       #else
        FSRef ref;
        LSItemInfoRecord info;

        return FSPathMakeRefWithOptions ((const UInt8*) path.toRawUTF8(), kFSPathMakeRefDoNotFollowLeafSymlink, &ref, 0) == noErr
                 && LSCopyItemInfoForRef (&ref, kLSRequestBasicFlagsOnly, &info) == noErr
                 && (info.flags & kLSItemInfoIsInvisible) != 0;
       #endif
    }

   #if BEAST_IOS
    String getIOSSystemLocation (NSSearchPathDirectory type)
    {
        return nsStringToBeast ([NSSearchPathForDirectoriesInDomains (type, NSUserDomainMask, YES)
                                objectAtIndex: 0]);
    }
   #endif

    static bool launchExecutable (const String& pathAndArguments)
    {
        const char* const argv[4] = { "/bin/sh", "-c", pathAndArguments.toUTF8(), 0 };

        const int cpid = fork();

        if (cpid == 0)
        {
            // Child process
            if (execve (argv[0], (char**) argv, 0) < 0)
                exit (0);
        }
        else
        {
            if (cpid < 0)
                return false;
        }

        return true;
    }
}

bool File::isOnCDRomDrive() const
{
    const char* const cdTypes[] = { "cd9660", "cdfs", "cddafs", "udf", 0 };

    return FileHelpers::isFileOnDriveType (*this, cdTypes);
}

bool File::isOnHardDisk() const
{
    const char* const nonHDTypes[] = { "nfs", "smbfs", "ramfs", 0 };

    return ! (isOnCDRomDrive() || FileHelpers::isFileOnDriveType (*this, nonHDTypes));
}

bool File::isOnRemovableDrive() const
{
   #if BEAST_IOS
    return false; // xxx is this possible?
   #else
    BEAST_AUTORELEASEPOOL
    {
        BOOL removable = false;

        [[NSWorkspace sharedWorkspace]
               getFileSystemInfoForPath: beastStringToNS (getFullPathName())
                            isRemovable: &removable
                             isWritable: nil
                          isUnmountable: nil
                            description: nil
                                   type: nil];

        return removable;
    }
   #endif
}

bool File::isHidden() const
{
    return FileHelpers::isHiddenFile (getFullPathName());
}

//==============================================================================
const char* const* beast_argv = nullptr;
int beast_argc = 0;

File File::getSpecialLocation (const SpecialLocationType type)
{
    BEAST_AUTORELEASEPOOL
    {
        String resultPath;

        switch (type)
        {
            case userHomeDirectory:                 resultPath = nsStringToBeast (NSHomeDirectory()); break;

          #if BEAST_IOS
            case userDocumentsDirectory:            resultPath = FileHelpers::getIOSSystemLocation (NSDocumentDirectory); break;
            case userDesktopDirectory:              resultPath = FileHelpers::getIOSSystemLocation (NSDesktopDirectory); break;

            case tempDirectory:
            {
                File tmp (FileHelpers::getIOSSystemLocation (NSCachesDirectory));
                tmp = tmp.getChildFile (beast_getExecutableFile().getFileNameWithoutExtension());
                tmp.createDirectory();
                return tmp.getFullPathName();
            }

          #else
            case userDocumentsDirectory:            resultPath = "~/Documents"; break;
            case userDesktopDirectory:              resultPath = "~/Desktop"; break;

            case tempDirectory:
            {
                File tmp ("~/Library/Caches/" + beast_getExecutableFile().getFileNameWithoutExtension());
                tmp.createDirectory();
                return tmp.getFullPathName();
            }
          #endif
            case userMusicDirectory:                resultPath = "~/Music"; break;
            case userMoviesDirectory:               resultPath = "~/Movies"; break;
            case userPicturesDirectory:             resultPath = "~/Pictures"; break;
            case userApplicationDataDirectory:      resultPath = "~/Library"; break;
            case commonApplicationDataDirectory:    resultPath = "/Library"; break;
            case commonDocumentsDirectory:          resultPath = "/Users/Shared"; break;
            case globalApplicationsDirectory:       resultPath = "/Applications"; break;

            case invokedExecutableFile:
                if (beast_argv != nullptr && beast_argc > 0)
                    return File (CharPointer_UTF8 (beast_argv[0]));
                // deliberate fall-through...

            case currentExecutableFile:
                return beast_getExecutableFile();

            case currentApplicationFile:
            {
                const File exe (beast_getExecutableFile());
                const File parent (exe.getParentDirectory());

              #if BEAST_IOS
                return parent;
              #else
                return parent.getFullPathName().endsWithIgnoreCase ("Contents/MacOS")
                        ? parent.getParentDirectory().getParentDirectory()
                        : exe;
              #endif
            }

            case hostApplicationPath:
            {
                unsigned int size = 8192;
                HeapBlock<char> buffer;
                buffer.calloc (size + 8);

                _NSGetExecutablePath (buffer.getData(), &size);
                return String::fromUTF8 (buffer, (int) size);
            }

            default:
                bassertfalse; // unknown type?
                break;
        }

        if (resultPath.isNotEmpty())
            return File (resultPath.convertToPrecomposedUnicode());
    }

    return File::nonexistent ();
}

//==============================================================================
String File::getVersion() const
{
    BEAST_AUTORELEASEPOOL
    {
        if (NSBundle* bundle = [NSBundle bundleWithPath: beastStringToNS (getFullPathName())])
            if (NSDictionary* info = [bundle infoDictionary])
                if (NSString* name = [info valueForKey: nsStringLiteral ("CFBundleShortVersionString")])
                    return nsStringToBeast (name);
    }

    return String::empty;
}

//==============================================================================
File File::getLinkedTarget() const
{
   #if BEAST_IOS || (defined (MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
    NSString* dest = [[NSFileManager defaultManager] destinationOfSymbolicLinkAtPath: beastStringToNS (getFullPathName()) error: nil];
   #else
    // (the cast here avoids a deprecation warning)
    NSString* dest = [((id) [NSFileManager defaultManager]) pathContentOfSymbolicLinkAtPath: beastStringToNS (getFullPathName())];
   #endif

    if (dest != nil)
        return File (nsStringToBeast (dest));

    return *this;
}

//==============================================================================
bool File::moveToTrash() const
{
    if (! exists())
        return true;

   #if BEAST_IOS
    return deleteFile(); //xxx is there a trashcan on the iOS?
   #else
    BEAST_AUTORELEASEPOOL
    {
        NSString* p = beastStringToNS (getFullPathName());

        return [[NSWorkspace sharedWorkspace]
                    performFileOperation: NSWorkspaceRecycleOperation
                                  source: [p stringByDeletingLastPathComponent]
                             destination: nsEmptyString()
                                   files: [NSArray arrayWithObject: [p lastPathComponent]]
                                     tag: nil ];
    }
   #endif
}

//==============================================================================
class DirectoryIterator::NativeIterator::Pimpl : public Uncopyable
{
public:
    Pimpl (const File& directory, const String& wildCard_)
        : parentDir (File::addTrailingSeparator (directory.getFullPathName())),
          wildCard (wildCard_),
          enumerator (nil)
    {
        BEAST_AUTORELEASEPOOL
        {
            enumerator = [[[NSFileManager defaultManager] enumeratorAtPath: beastStringToNS (directory.getFullPathName())] retain];
        }
    }

    ~Pimpl()
    {
        [enumerator release];
    }

    bool next (String& filenameFound,
               bool* const isDir, bool* const isHidden, std::int64_t* const fileSize,
               Time* const modTime, Time* const creationTime, bool* const isReadOnly)
    {
        BEAST_AUTORELEASEPOOL
        {
            const char* wildcardUTF8 = nullptr;

            for (;;)
            {
                NSString* file;
                if (enumerator == nil || (file = [enumerator nextObject]) == nil)
                    return false;

                [enumerator skipDescendents];
                filenameFound = nsStringToBeast (file);

                if (wildcardUTF8 == nullptr)
                    wildcardUTF8 = wildCard.toUTF8();

                if (fnmatch (wildcardUTF8, filenameFound.toUTF8(), FNM_CASEFOLD) != 0)
                    continue;

                const String fullPath (parentDir + filenameFound);
                updateStatInfoForFile (fullPath, isDir, fileSize, modTime, creationTime, isReadOnly);

                if (isHidden != nullptr)
                    *isHidden = FileHelpers::isHiddenFile (fullPath);

                return true;
            }
        }
    }

private:
    String parentDir, wildCard;
    NSDirectoryEnumerator* enumerator;
};

DirectoryIterator::NativeIterator::NativeIterator (const File& directory, const String& wildcard)
    : pimpl (new DirectoryIterator::NativeIterator::Pimpl (directory, wildcard))
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
  #if BEAST_IOS
    return [[UIApplication sharedApplication] openURL: [NSURL URLWithString: beastStringToNS (fileName)]];
  #else
    BEAST_AUTORELEASEPOOL
    {
        if (parameters.isEmpty())
        {
            return [[NSWorkspace sharedWorkspace] openFile: beastStringToNS (fileName)]
                || [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: beastStringToNS (fileName)]];
        }

        bool ok = false;
        const File file (fileName);

        if (file.isBundle())
        {
            NSMutableArray* urls = [NSMutableArray array];

            StringArray docs;
            docs.addTokens (parameters, true);
            for (int i = 0; i < docs.size(); ++i)
                [urls addObject: beastStringToNS (docs[i])];

            ok = [[NSWorkspace sharedWorkspace] openURLs: urls
                                 withAppBundleIdentifier: [[NSBundle bundleWithPath: beastStringToNS (fileName)] bundleIdentifier]
                                                 options: 0
                          additionalEventParamDescriptor: nil
                                       launchIdentifiers: nil];
        }
        else if (file.exists())
        {
            ok = FileHelpers::launchExecutable ("\"" + fileName + "\" " + parameters);
        }

        return ok;
    }
  #endif
}

void File::revealToUser() const
{
   #if ! BEAST_IOS
    if (exists())
        [[NSWorkspace sharedWorkspace] selectFile: beastStringToNS (getFullPathName()) inFileViewerRootedAtPath: nsEmptyString()];
    else if (getParentDirectory().exists())
        getParentDirectory().revealToUser();
   #endif
}

//==============================================================================
OSType File::getMacOSType() const
{
    BEAST_AUTORELEASEPOOL
    {
       #if BEAST_IOS || (defined (MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5)
        NSDictionary* fileDict = [[NSFileManager defaultManager] attributesOfItemAtPath: beastStringToNS (getFullPathName()) error: nil];
       #else
        // (the cast here avoids a deprecation warning)
        NSDictionary* fileDict = [((id) [NSFileManager defaultManager]) fileAttributesAtPath: beastStringToNS (getFullPathName()) traverseLink: NO];
       #endif

        return [fileDict fileHFSTypeCode];
    }
}

bool File::isBundle() const
{
   #if BEAST_IOS
    return false; // xxx can't find a sensible way to do this without trying to open the bundle..
   #else
    BEAST_AUTORELEASEPOOL
    {
        return [[NSWorkspace sharedWorkspace] isFilePackageAtPath: beastStringToNS (getFullPathName())];
    }
   #endif
}

#if BEAST_MAC
void File::addToDock() const
{
    // check that it's not already there...
    if (! beast_getOutputFromCommand ("defaults read com.apple.dock persistent-apps").containsIgnoreCase (getFullPathName()))
    {
        beast_runSystemCommand ("defaults write com.apple.dock persistent-apps -array-add \"<dict><key>tile-data</key><dict><key>file-data</key><dict><key>_CFURLString</key><string>"
                                 + getFullPathName() + "</string><key>_CFURLStringType</key><integer>0</integer></dict></dict></dict>\"");

        beast_runSystemCommand ("osascript -e \"tell application \\\"Dock\\\" to quit\"");
    }
}
#endif

} // beast
