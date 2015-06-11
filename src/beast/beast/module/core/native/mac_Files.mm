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

//==============================================================================
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
class DirectoryIterator::NativeIterator::Pimpl
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

    Pimpl (Pimpl const&) = delete;
    Pimpl& operator= (Pimpl const&) = delete;

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

} // beast
