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

#ifndef BEAST_MODULE_CORE_FILES_FILE_H_INCLUDED
#define BEAST_MODULE_CORE_FILES_FILE_H_INCLUDED

#include <beast/module/core/containers/Array.h>
#include <beast/module/core/memory/MemoryBlock.h>
#include <beast/module/core/misc/Result.h>
#include <beast/module/core/text/StringArray.h>
#include <beast/module/core/threads/CriticalSection.h>

namespace beast {

class FileInputStream;
class FileOutputStream;

//==============================================================================
/**
    Represents a local file or directory.

    This class encapsulates the absolute pathname of a file or directory, and
    has methods for finding out about the file and changing its properties.

    To read or write to the file, there are methods for returning an input or
    output stream.

    @see FileInputStream, FileOutputStream
*/
class  File
{
public:
    //==============================================================================
    /** Creates an (invalid) file object.

        The file is initially set to an empty path, so getFullPath() will return
        an empty string, and comparing the file to File::nonexistent will return
        true.

        You can use its operator= method to point it at a proper file.
    */
    File() noexcept  {}

    /** Creates a file from an absolute path.

        If the path supplied is a relative path, it is taken to be relative
        to the current working directory (see File::getCurrentWorkingDirectory()),
        but this isn't a recommended way of creating a file, because you
        never know what the CWD is going to be.

        On the Mac/Linux, the path can include "~" notation for referring to
        user home directories.
    */
    File (const String& absolutePath);

    /** Creates a copy of another file object. */
    File (const File&);

    /** Destructor. */
    ~File() noexcept  {}

    /** Sets the file based on an absolute pathname.

        If the path supplied is a relative path, it is taken to be relative
        to the current working directory (see File::getCurrentWorkingDirectory()),
        but this isn't a recommended way of creating a file, because you
        never know what the CWD is going to be.

        On the Mac/Linux, the path can include "~" notation for referring to
        user home directories.
    */
    File& operator= (const String& newAbsolutePath);

    /** Copies from another file object. */
    File& operator= (const File& otherFile);

   #if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    File (File&&) noexcept;
    File& operator= (File&&) noexcept;
   #endif

    //==============================================================================
    /** This static constant is used for referring to an 'invalid' file. */
    static File const& nonexistent ();

    //==============================================================================
    /** Checks whether the file actually exists.

        @returns    true if the file exists, either as a file or a directory.
        @see existsAsFile, isDirectory
    */
    bool exists() const;

    /** Checks whether the file exists and is a file rather than a directory.

        @returns    true only if this is a real file, false if it's a directory
                    or doesn't exist
        @see exists, isDirectory
    */
    bool existsAsFile() const;

    /** Checks whether the file is a directory that exists.

        @returns    true only if the file is a directory which actually exists, so
                    false if it's a file or doesn't exist at all
        @see exists, existsAsFile
    */
    bool isDirectory() const;

    /** Returns the size of the file in bytes.

        @returns    the number of bytes in the file, or 0 if it doesn't exist.
    */
    std::int64_t getSize() const;

    //==============================================================================
    /** Returns the complete, absolute path of this file.

        This includes the filename and all its parent folders. On Windows it'll
        also include the drive letter prefix; on Mac or Linux it'll be a complete
        path starting from the root folder.

        If you just want the file's name, you should use getFileName() or
        getFileNameWithoutExtension().

        @see getFileName
    */
    const String& getFullPathName() const noexcept          { return fullPath; }

    /** Returns the last section of the pathname.

        Returns just the final part of the path - e.g. if the whole path
        is "/moose/fish/foo.txt" this will return "foo.txt".

        For a directory, it returns the final part of the path - e.g. for the
        directory "/moose/fish" it'll return "fish".

        If the filename begins with a dot, it'll return the whole filename, e.g. for
        "/moose/.fish", it'll return ".fish"

        @see getFullPathName, getFileNameWithoutExtension
    */
    String getFileName() const;

    //==============================================================================
    /** Returns the file's extension.

        Returns the file extension of this file, also including the dot.

        e.g. "/moose/fish/foo.txt" would return ".txt"

        @see hasFileExtension, withFileExtension, getFileNameWithoutExtension
    */
    String getFileExtension() const;

    /** Checks whether the file has a given extension.

        @param extensionToTest  the extension to look for - it doesn't matter whether or
                                not this string has a dot at the start, so ".wav" and "wav"
                                will have the same effect. To compare with multiple extensions, this
                                parameter can contain multiple strings, separated by semi-colons -
                                so, for example: hasFileExtension (".jpeg;png;gif") would return
                                true if the file has any of those three extensions.

        @see getFileExtension, withFileExtension, getFileNameWithoutExtension
    */
    bool hasFileExtension (const String& extensionToTest) const;

    /** Returns a version of this file with a different file extension.

        e.g. File ("/moose/fish/foo.txt").withFileExtension ("html") returns "/moose/fish/foo.html"

        @param newExtension     the new extension, either with or without a dot at the start (this
                                doesn't make any difference). To get remove a file's extension altogether,
                                pass an empty string into this function.

        @see getFileName, getFileExtension, hasFileExtension, getFileNameWithoutExtension
    */
    File withFileExtension (const String& newExtension) const;

    /** Returns the last part of the filename, without its file extension.

        e.g. for "/moose/fish/foo.txt" this will return "foo".

        @see getFileName, getFileExtension, hasFileExtension, withFileExtension
    */
    String getFileNameWithoutExtension() const;

    //==============================================================================
    /** Returns a file that represents a relative (or absolute) sub-path of the current one.

        This will find a child file or directory of the current object.

        e.g.
            File ("/moose/fish").getChildFile ("foo.txt") will produce "/moose/fish/foo.txt".
            File ("/moose/fish").getChildFile ("haddock/foo.txt") will produce "/moose/fish/haddock/foo.txt".
            File ("/moose/fish").getChildFile ("../foo.txt") will produce "/moose/foo.txt".

        If the string is actually an absolute path, it will be treated as such, e.g.
            File ("/moose/fish").getChildFile ("/foo.txt") will produce "/foo.txt"

        @see getSiblingFile, getParentDirectory, isAChildOf
    */
    File getChildFile (String relativeOrAbsolutePath) const;

    /** Returns a file which is in the same directory as this one.

        This is equivalent to getParentDirectory().getChildFile (name).

        @see getChildFile, getParentDirectory
    */
    File getSiblingFile (const String& siblingFileName) const;

    //==============================================================================
    /** Returns the directory that contains this file or directory.

        e.g. for "/moose/fish/foo.txt" this will return "/moose/fish".
    */
    File getParentDirectory() const;

    /** Checks whether a file is somewhere inside a directory.

        Returns true if this file is somewhere inside a subdirectory of the directory
        that is passed in. Neither file actually has to exist, because the function
        just checks the paths for similarities.

        e.g. File ("/moose/fish/foo.txt").isAChildOf ("/moose") is true.
             File ("/moose/fish/foo.txt").isAChildOf ("/moose/fish") is also true.
    */
    bool isAChildOf (const File& potentialParentDirectory) const;

    //==============================================================================
    /** Compares the pathnames for two files. */
    bool operator== (const File&) const;
    /** Compares the pathnames for two files. */
    bool operator!= (const File&) const;
    /** Compares the pathnames for two files. */
    bool operator< (const File&) const;
    /** Compares the pathnames for two files. */
    bool operator> (const File&) const;

    //==============================================================================
    /** Creates an empty file if it doesn't already exist.

        If the file that this object refers to doesn't exist, this will create a file
        of zero size.

        If it already exists or is a directory, this method will do nothing.

        @returns    true if the file has been created (or if it already existed).
        @see createDirectory
    */
    Result create() const;

    /** Creates a new directory for this filename.

        This will try to create the file as a directory, and fill also create
        any parent directories it needs in order to complete the operation.

        @returns    a result to indicate whether the directory was created successfully, or
                    an error message if it failed.
        @see create
    */
    Result createDirectory() const;

    /** Deletes a file.

        If this file is actually a directory, it may not be deleted correctly if it
        contains files. See deleteRecursively() as a better way of deleting directories.

        @returns    true if the file has been successfully deleted (or if it didn't exist to
                    begin with).
        @see deleteRecursively
    */
    bool deleteFile() const;

    /** Deletes a file or directory and all its subdirectories.

        If this file is a directory, this will try to delete it and all its subfolders. If
        it's just a file, it will just try to delete the file.

        @returns    true if the file and all its subfolders have been successfully deleted
                    (or if it didn't exist to begin with).
        @see deleteFile
    */
    bool deleteRecursively() const;

    //==============================================================================
    /** Used in file searching, to specify whether to return files, directories, or both.
    */
    enum TypesOfFileToFind
    {
        findDirectories             = 1,    /**< Use this flag to indicate that you want to find directories. */
        findFiles                   = 2,    /**< Use this flag to indicate that you want to find files. */
        findFilesAndDirectories     = 3,    /**< Use this flag to indicate that you want to find both files and directories. */
        ignoreHiddenFiles           = 4     /**< Add this flag to avoid returning any hidden files in the results. */
    };

    /** Searches inside a directory for files matching a wildcard pattern.

        Assuming that this file is a directory, this method will search it
        for either files or subdirectories whose names match a filename pattern.

        @param results                  an array to which File objects will be added for the
                                        files that the search comes up with
        @param whatToLookFor            a value from the TypesOfFileToFind enum, specifying whether to
                                        return files, directories, or both. If the ignoreHiddenFiles flag
                                        is also added to this value, hidden files won't be returned
        @param searchRecursively        if true, all subdirectories will be recursed into to do
                                        an exhaustive search
        @param wildCardPattern          the filename pattern to search for, e.g. "*.txt"
        @returns                        the number of results that have been found

        @see getNumberOfChildFiles, DirectoryIterator
    */
    int findChildFiles (Array<File>& results,
                        int whatToLookFor,
                        bool searchRecursively,
                        const String& wildCardPattern = "*") const;

    /** Searches inside a directory and counts how many files match a wildcard pattern.

        Assuming that this file is a directory, this method will search it
        for either files or subdirectories whose names match a filename pattern,
        and will return the number of matches found.

        This isn't a recursive call, and will only search this directory, not
        its children.

        @param whatToLookFor    a value from the TypesOfFileToFind enum, specifying whether to
                                count files, directories, or both. If the ignoreHiddenFiles flag
                                is also added to this value, hidden files won't be counted
        @param wildCardPattern  the filename pattern to search for, e.g. "*.txt"
        @returns                the number of matches found
        @see findChildFiles, DirectoryIterator
    */
    int getNumberOfChildFiles (int whatToLookFor,
                               const String& wildCardPattern = "*") const;

    //==============================================================================
    /** Creates a stream to read from this file.

        @returns    a stream that will read from this file (initially positioned at the
                    start of the file), or nullptr if the file can't be opened for some reason
        @see createOutputStream
    */
    FileInputStream* createInputStream() const;

    /** Creates a stream to write to this file.

        If the file exists, the stream that is returned will be positioned ready for
        writing at the end of the file, so you might want to use deleteFile() first
        to write to an empty file.

        @returns    a stream that will write to this file (initially positioned at the
                    end of the file), or nullptr if the file can't be opened for some reason
        @see createInputStream, appendData, appendText
    */
    FileOutputStream* createOutputStream (size_t bufferSize = 0x8000) const;

    //==============================================================================
    /** Appends a block of binary data to the end of the file.

        This will try to write the given buffer to the end of the file.

        @returns false if it can't write to the file for some reason
    */
    bool appendData (const void* dataToAppend,
                     size_t numberOfBytes) const;

    /** Appends a string to the end of the file.

        This will try to append a text string to the file, as either 16-bit unicode
        or 8-bit characters in the default system encoding.

        It can also write the 'ff fe' unicode header bytes before the text to indicate
        the endianness of the file.

        Any single \\n characters in the string are replaced with \\r\\n before it is written.

        @see replaceWithText
    */
    bool appendText (const String& textToAppend,
                     bool asUnicode = false,
                     bool writeUnicodeHeaderBytes = false) const;

    //==============================================================================
    /** A set of types of location that can be passed to the getSpecialLocation() method.
    */
    enum SpecialLocationType
    {
        /** The user's home folder. This is the same as using File ("~"). */
        userHomeDirectory,

        /** The user's default documents folder. On Windows, this might be the user's
            "My Documents" folder. On the Mac it'll be their "Documents" folder. Linux
            doesn't tend to have one of these, so it might just return their home folder.
        */
        userDocumentsDirectory,

        /** The folder that contains the user's desktop objects. */
        userDesktopDirectory,

        /** The most likely place where a user might store their music files. */
        userMusicDirectory,

        /** The most likely place where a user might store their movie files. */
        userMoviesDirectory,

        /** The most likely place where a user might store their picture files. */
        userPicturesDirectory,

        /** The folder in which applications store their persistent user-specific settings.
            On Windows, this might be "\Documents and Settings\username\Application Data".
            On the Mac, it might be "~/Library". If you're going to store your settings in here,
            always create your own sub-folder to put them in, to avoid making a mess.
        */
        userApplicationDataDirectory,

        /** An equivalent of the userApplicationDataDirectory folder that is shared by all users
            of the computer, rather than just the current user.

            On the Mac it'll be "/Library", on Windows, it could be something like
            "\Documents and Settings\All Users\Application Data".

            Depending on the setup, this folder may be read-only.
        */
        commonApplicationDataDirectory,

        /** A place to put documents which are shared by all users of the machine.
            On Windows this may be somewhere like "C:\Users\Public\Documents", on OSX it
            will be something like "/Users/Shared". Other OSes may have no such concept
            though, so be careful.
        */
        commonDocumentsDirectory,

        /** The folder that should be used for temporary files.
            Always delete them when you're finished, to keep the user's computer tidy!
        */
        tempDirectory,

        /** The directory in which applications normally get installed.
            So on windows, this would be something like "c:\program files", on the
            Mac "/Applications", or "/usr" on linux.
        */
        globalApplicationsDirectory
    };

    /** Finds the location of a special type of file or directory, such as a home folder or
        documents folder.

        @see SpecialLocationType
    */
    static File getSpecialLocation (const SpecialLocationType type);

    //==============================================================================
    /** Returns a temporary file in the system's temp directory.
        This will try to return the name of a non-existent temp file.
        To get the temp folder, you can use getSpecialLocation (File::tempDirectory).
    */
    static File createTempFile (const String& fileNameEnding);


    //==============================================================================
    /** Returns the current working directory. */
    static File getCurrentWorkingDirectory();

    //==============================================================================
    /** The system-specific file separator character.
        On Windows, this will be '\', on Mac/Linux, it'll be '/'
    */
    static const beast_wchar separator;

    /** The system-specific file separator character, as a string.
        On Windows, this will be '\', on Mac/Linux, it'll be '/'
    */
    static const String separatorString;

    //==============================================================================
    /** Indicates whether filenames are case-sensitive on the current operating system. */
    static bool areFileNamesCaseSensitive();

    /** Returns true if the string seems to be a fully-specified absolute path. */
    static bool isAbsolutePath (const String& path);

    /** Creates a file that simply contains this string, without doing the sanity-checking
        that the normal constructors do.

        Best to avoid this unless you really know what you're doing.
    */
    static File createFileWithoutCheckingPath (const String& absolutePath) noexcept;

    /** Adds a separator character to the end of a path if it doesn't already have one. */
    static String addTrailingSeparator (const String& path);

private:
    //==============================================================================
    String fullPath;

    static String parseAbsolutePath (const String&);
    String getPathUpToLastSlash() const;

    Result createDirectoryInternal (const String&) const;
};

} // beast

#endif

