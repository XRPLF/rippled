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

#ifndef BEAST_DIRECTORYITERATOR_H_INCLUDED
#define BEAST_DIRECTORYITERATOR_H_INCLUDED

#include <memory>

namespace beast {

//==============================================================================
/**
    Searches through a the files in a directory, returning each file that is found.

    A DirectoryIterator will search through a directory and its subdirectories using
    a wildcard filepattern match.

    If you may be finding a large number of files, this is better than
    using File::findChildFiles() because it doesn't block while it finds them
    all, and this is more memory-efficient.

    It can also guess how far it's got using a wildly inaccurate algorithm.
*/
class DirectoryIterator : LeakChecked <DirectoryIterator>, public Uncopyable
{
public:
    //==============================================================================
    /** Creates a DirectoryIterator for a given directory.

        After creating one of these, call its next() method to get the
        first file - e.g. @code

        DirectoryIterator iter (File ("/animals/mooses"), true, "*.moose");

        while (iter.next())
        {
            File theFileItFound (iter.getFile());

            ... etc
        }
        @endcode

        @param directory    the directory to search in
        @param isRecursive  whether all the subdirectories should also be searched
        @param wildCard     the file pattern to match. This may contain multiple patterns
                            separated by a semi-colon or comma, e.g. "*.jpg;*.png"
        @param whatToLookFor    a value from the File::TypesOfFileToFind enum, specifying
                                whether to look for files, directories, or both.
    */
    DirectoryIterator (const File& directory,
                       bool isRecursive,
                       const String& wildCard = "*",
                       int whatToLookFor = File::findFiles);

    /** Destructor. */
    ~DirectoryIterator();

    /** Moves the iterator along to the next file.

        @returns    true if a file was found (you can then use getFile() to see what it was) - or
                    false if there are no more matching files.
    */
    bool next();

    /** Moves the iterator along to the next file, and returns various properties of that file.

        If you need to find out details about the file, it's more efficient to call this method than
        to call the normal next() method and then find out the details afterwards.

        All the parameters are optional, so pass null pointers for any items that you're not
        interested in.

        @returns    true if a file was found (you can then use getFile() to see what it was) - or
                    false if there are no more matching files. If it returns false, then none of the
                    parameters will be filled-in.
    */
    bool next (bool* isDirectory,
               bool* isHidden,
               std::int64_t* fileSize,
               Time* modTime,
               Time* creationTime,
               bool* isReadOnly);

    /** Returns the file that the iterator is currently pointing at.

        The result of this call is only valid after a call to next() has returned true.
    */
    const File& getFile() const;

    /** Returns a guess of how far through the search the iterator has got.

        @returns    a value 0.0 to 1.0 to show the progress, although this won't be
                    very accurate.
    */
    float getEstimatedProgress() const;

private:
    //==============================================================================
    class NativeIterator : LeakChecked <NativeIterator>, public Uncopyable
    {
    public:
        NativeIterator (const File& directory, const String& wildCard);
        ~NativeIterator();

        bool next (String& filenameFound,
                   bool* isDirectory, bool* isHidden, std::int64_t* fileSize,
                   Time* modTime, Time* creationTime, bool* isReadOnly);

        class Pimpl;

    private:
        friend class DirectoryIterator;
        std::unique_ptr<Pimpl> pimpl;
    };

    StringArray wildCards;
    NativeIterator fileFinder;
    String wildCard, path;
    int index;
    mutable int totalNumFiles;
    const int whatToLookFor;
    const bool isRecursive;
    bool hasBeenAdvanced;
    std::unique_ptr <DirectoryIterator> subIterator;
    File currentFile;
};

} // beast

#endif   // BEAST_DIRECTORYITERATOR_H_INCLUDED
