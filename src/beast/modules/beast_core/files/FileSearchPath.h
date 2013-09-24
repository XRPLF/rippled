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

#ifndef BEAST_FILESEARCHPATH_H_INCLUDED
#define BEAST_FILESEARCHPATH_H_INCLUDED

//==============================================================================
/**
    Encapsulates a set of folders that make up a search path.

    @see File
*/
class BEAST_API FileSearchPath : LeakChecked <FileSearchPath>
{
public:
    //==============================================================================
    /** Creates an empty search path. */
    FileSearchPath();

    /** Creates a search path from a string of pathnames.

        The path can be semicolon- or comma-separated, e.g.
        "/foo/bar;/foo/moose;/fish/moose"

        The separate folders are tokenised and added to the search path.
    */
    FileSearchPath (const String& path);

    /** Creates a copy of another search path. */
    FileSearchPath (const FileSearchPath& other);

    /** Destructor. */
    ~FileSearchPath();

    /** Uses a string containing a list of pathnames to re-initialise this list.

        This search path is cleared and the semicolon- or comma-separated folders
        in this string are added instead. e.g. "/foo/bar;/foo/moose;/fish/moose"
    */
    FileSearchPath& operator= (const String& path);

    //==============================================================================
    /** Returns the number of folders in this search path.

        @see operator[]
    */
    int getNumPaths() const;

    /** Returns one of the folders in this search path.

        The file returned isn't guaranteed to actually be a valid directory.

        @see getNumPaths
    */
    File operator[] (int index) const;

    /** Returns the search path as a semicolon-separated list of directories. */
    String toString() const;

    //==============================================================================
    /** Adds a new directory to the search path.

        The new directory is added to the end of the list if the insertIndex parameter is
        less than zero, otherwise it is inserted at the given index.
    */
    void add (const File& directoryToAdd,
              int insertIndex = -1);

    /** Adds a new directory to the search path if it's not already in there. */
    void addIfNotAlreadyThere (const File& directoryToAdd);

    /** Removes a directory from the search path. */
    void remove (int indexToRemove);

    /** Merges another search path into this one.

        This will remove any duplicate directories.
    */
    void addPath (const FileSearchPath& other);

    /** Removes any directories that are actually subdirectories of one of the other directories in the search path.

        If the search is intended to be recursive, there's no point having nested folders in the search
        path, because they'll just get searched twice and you'll get duplicate results.

        e.g. if the path is "c:\abc\de;c:\abc", this method will simplify it to "c:\abc"
    */
    void removeRedundantPaths();

    /** Removes any directories that don't actually exist. */
    void removeNonExistentPaths();

    //==============================================================================
    /** Searches the path for a wildcard.

        This will search all the directories in the search path in order, adding any
        matching files to the results array.

        @param results                  an array to append the results to
        @param whatToLookFor            a value from the File::TypesOfFileToFind enum, specifying whether to
                                        return files, directories, or both.
        @param searchRecursively        whether to recursively search the subdirectories too
        @param wildCardPattern          a pattern to match against the filenames
        @returns the number of files added to the array
        @see File::findChildFiles
    */
    int findChildFiles (Array<File>& results,
                        int whatToLookFor,
                        bool searchRecursively,
                        const String& wildCardPattern = "*") const;

    //==============================================================================
    /** Finds out whether a file is inside one of the path's directories.

        This will return true if the specified file is a child of one of the
        directories specified by this path. Note that this doesn't actually do any
        searching or check that the files exist - it just looks at the pathnames
        to work out whether the file would be inside a directory.

        @param fileToCheck      the file to look for
        @param checkRecursively if true, then this will return true if the file is inside a
                                subfolder of one of the path's directories (at any depth). If false
                                it will only return true if the file is actually a direct child
                                of one of the directories.
        @see File::isAChildOf

    */
    bool isFileInPath (const File& fileToCheck,
                       bool checkRecursively) const;

private:
    //==============================================================================
    StringArray directories;

    void init (const String& path);
};

#endif   // BEAST_FILESEARCHPATH_H_INCLUDED
