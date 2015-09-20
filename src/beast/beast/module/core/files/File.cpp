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

#include <algorithm>
#include <memory>

namespace beast {

File const& File::nonexistent()
{
    static File const instance;
    return instance;
}

//------------------------------------------------------------------------------

File::File (const String& fullPathName)
    : fullPath (parseAbsolutePath (fullPathName))
{
}

File::File (const File& other)
    : fullPath (other.fullPath)
{
}

File File::createFileWithoutCheckingPath (const String& path) noexcept
{
    File f;
    f.fullPath = path;
    return f;
}

File& File::operator= (const String& newPath)
{
    fullPath = parseAbsolutePath (newPath);
    return *this;
}

File& File::operator= (const File& other)
{
    fullPath = other.fullPath;
    return *this;
}

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
File::File (File&& other) noexcept
    : fullPath (static_cast <String&&> (other.fullPath))
{
}

File& File::operator= (File&& other) noexcept
{
    fullPath = static_cast <String&&> (other.fullPath);
    return *this;
}
#endif

//==============================================================================
String File::parseAbsolutePath (const String& p)
{
    if (p.isEmpty())
        return String::empty;

#if BEAST_WINDOWS
    // Windows..
    String path (p.replaceCharacter ('/', '\\'));

    if (path.startsWithChar (separator))
    {
        if (path[1] != separator)
        {
            /*  When you supply a raw string to the File object constructor, it must be an absolute path.
                If you're trying to parse a string that may be either a relative path or an absolute path,
                you MUST provide a context against which the partial path can be evaluated - you can do
                this by simply using File::getChildFile() instead of the File constructor. E.g. saying
                "File::getCurrentWorkingDirectory().getChildFile (myUnknownPath)" would return an absolute
                path if that's what was supplied, or would evaluate a partial path relative to the CWD.
            */
            bassertfalse;

            path = File::getCurrentWorkingDirectory().getFullPathName().substring (0, 2) + path;
        }
    }
    else if (! path.containsChar (':'))
    {
        /*  When you supply a raw string to the File object constructor, it must be an absolute path.
            If you're trying to parse a string that may be either a relative path or an absolute path,
            you MUST provide a context against which the partial path can be evaluated - you can do
            this by simply using File::getChildFile() instead of the File constructor. E.g. saying
            "File::getCurrentWorkingDirectory().getChildFile (myUnknownPath)" would return an absolute
            path if that's what was supplied, or would evaluate a partial path relative to the CWD.
        */
        bassertfalse;

        return File::getCurrentWorkingDirectory().getChildFile (path).getFullPathName();
    }
#else
    // Mac or Linux..

    // Yes, I know it's legal for a unix pathname to contain a backslash, but this assertion is here
    // to catch anyone who's trying to run code that was written on Windows with hard-coded path names.
    // If that's why you've ended up here, use File::getChildFile() to build your paths instead.
    bassert ((! p.containsChar ('\\')) || (p.indexOfChar ('/') >= 0 && p.indexOfChar ('/') < p.indexOfChar ('\\')));

    String path (p);

    if (path.startsWithChar ('~'))
    {
        if (path[1] == separator || path[1] == 0)
        {
            // expand a name of the form "~/abc"
            path = File::getSpecialLocation (File::userHomeDirectory).getFullPathName()
                    + path.substring (1);
        }
        else
        {
            // expand a name of type "~dave/abc"
            const String userName (path.substring (1).upToFirstOccurrenceOf ("/", false, false));

            if (struct passwd* const pw = getpwnam (userName.toUTF8()))
                path = addTrailingSeparator (pw->pw_dir) + path.fromFirstOccurrenceOf ("/", false, false);
        }
    }
    else if (! path.startsWithChar (separator))
    {
       #if BEAST_DEBUG || BEAST_LOG_ASSERTIONS
        if (! (path.startsWith ("./") || path.startsWith ("../")))
        {
            /*  When you supply a raw string to the File object constructor, it must be an absolute path.
                If you're trying to parse a string that may be either a relative path or an absolute path,
                you MUST provide a context against which the partial path can be evaluated - you can do
                this by simply using File::getChildFile() instead of the File constructor. E.g. saying
                "File::getCurrentWorkingDirectory().getChildFile (myUnknownPath)" would return an absolute
                path if that's what was supplied, or would evaluate a partial path relative to the CWD.
            */
            bassertfalse;
        }
       #endif

        return File::getCurrentWorkingDirectory().getChildFile (path).getFullPathName();
    }
#endif

    while (path.endsWithChar (separator) && path != separatorString) // careful not to turn a single "/" into an empty string.
        path = path.dropLastCharacters (1);

    return path;
}

String File::addTrailingSeparator (const String& path)
{
    return path.endsWithChar (separator) ? path
                                         : path + separator;
}

//==============================================================================
#if BEAST_LINUX
 #define NAMES_ARE_CASE_SENSITIVE 1
#endif

bool File::areFileNamesCaseSensitive()
{
   #if NAMES_ARE_CASE_SENSITIVE
    return true;
   #else
    return false;
   #endif
}

static int compareFilenames (const String& name1, const String& name2) noexcept
{
   #if NAMES_ARE_CASE_SENSITIVE
    return name1.compare (name2);
   #else
    return name1.compareIgnoreCase (name2);
   #endif
}

bool File::operator== (const File& other) const     { return compareFilenames (fullPath, other.fullPath) == 0; }
bool File::operator!= (const File& other) const     { return compareFilenames (fullPath, other.fullPath) != 0; }
bool File::operator<  (const File& other) const     { return compareFilenames (fullPath, other.fullPath) <  0; }
bool File::operator>  (const File& other) const     { return compareFilenames (fullPath, other.fullPath) >  0; }

//==============================================================================
bool File::deleteRecursively() const
{
    bool worked = true;

    if (isDirectory())
    {
        Array<File> subFiles;
        findChildFiles (subFiles, File::findFilesAndDirectories, false);

        for (int i = subFiles.size(); --i >= 0;)
            worked = subFiles.getReference(i).deleteRecursively() && worked;
    }

    return deleteFile() && worked;
}

//==============================================================================
String File::getPathUpToLastSlash() const
{
    const int lastSlash = fullPath.lastIndexOfChar (separator);

    if (lastSlash > 0)
        return fullPath.substring (0, lastSlash);

    if (lastSlash == 0)
        return separatorString;

    return fullPath;
}

File File::getParentDirectory() const
{
    File f;
    f.fullPath = getPathUpToLastSlash();
    return f;
}

//==============================================================================
String File::getFileName() const
{
    return fullPath.substring (fullPath.lastIndexOfChar (separator) + 1);
}

String File::getFileNameWithoutExtension() const
{
    const int lastSlash = fullPath.lastIndexOfChar (separator) + 1;
    const int lastDot   = fullPath.lastIndexOfChar ('.');

    if (lastDot > lastSlash)
        return fullPath.substring (lastSlash, lastDot);

    return fullPath.substring (lastSlash);
}

bool File::isAChildOf (const File& potentialParent) const
{
    if (potentialParent == File::nonexistent ())
        return false;

    const String ourPath (getPathUpToLastSlash());

    if (compareFilenames (potentialParent.fullPath, ourPath) == 0)
        return true;

    if (potentialParent.fullPath.length() >= ourPath.length())
        return false;

    return getParentDirectory().isAChildOf (potentialParent);
}

//==============================================================================
bool File::isAbsolutePath (const String& path)
{
    return path.startsWithChar (separator)
           #if BEAST_WINDOWS
            || (path.isNotEmpty() && path[1] == ':');
           #else
            || path.startsWithChar ('~');
           #endif
}

File File::getChildFile (String relativePath) const
{
    if (isAbsolutePath (relativePath))
        return File (relativePath);

    String path (fullPath);

    // It's relative, so remove any ../ or ./ bits at the start..
    if (relativePath[0] == '.')
    {
       #if BEAST_WINDOWS
        relativePath = relativePath.replaceCharacter ('/', '\\');
       #endif

        while (relativePath[0] == '.')
        {
            const beast_wchar secondChar = relativePath[1];

            if (secondChar == '.')
            {
                const beast_wchar thirdChar = relativePath[2];

                if (thirdChar == 0 || thirdChar == separator)
                {
                    const int lastSlash = path.lastIndexOfChar (separator);
                    if (lastSlash >= 0)
                        path = path.substring (0, lastSlash);

                    relativePath = relativePath.substring (3);
                }
                else
                {
                    break;
                }
            }
            else if (secondChar == separator)
            {
                relativePath = relativePath.substring (2);
            }
            else
            {
                break;
            }
        }
    }

    return File (addTrailingSeparator (path) + relativePath);
}

File File::getSiblingFile (const String& fileName) const
{
    return getParentDirectory().getChildFile (fileName);
}

//==============================================================================
Result File::create() const
{
    if (exists())
        return Result::ok();

    const File parentDir (getParentDirectory());

    if (parentDir == *this)
        return Result::fail ("Cannot create parent directory");

    Result r (parentDir.createDirectory());

    if (r.wasOk())
    {
        FileOutputStream fo (*this, 8);
        r = fo.getStatus();
    }

    return r;
}

Result File::createDirectory() const
{
    if (isDirectory())
        return Result::ok();

    const File parentDir (getParentDirectory());

    if (parentDir == *this)
        return Result::fail ("Cannot create parent directory");

    Result r (parentDir.createDirectory());

    if (r.wasOk())
        r = createDirectoryInternal (fullPath.trimCharactersAtEnd (separatorString));

    return r;
}

//==============================================================================
int File::findChildFiles (Array<File>& results,
                          const int whatToLookFor,
                          const bool searchRecursively,
                          const String& wildCardPattern) const
{
    DirectoryIterator di (*this, searchRecursively, wildCardPattern, whatToLookFor);

    int total = 0;
    while (di.next())
    {
        results.add (di.getFile());
        ++total;
    }

    return total;
}

int File::getNumberOfChildFiles (const int whatToLookFor, const String& wildCardPattern) const
{
    DirectoryIterator di (*this, false, wildCardPattern, whatToLookFor);

    int total = 0;
    while (di.next())
        ++total;

    return total;
}

//==============================================================================
String File::getFileExtension() const
{
    const int indexOfDot = fullPath.lastIndexOfChar ('.');

    if (indexOfDot > fullPath.lastIndexOfChar (separator))
        return fullPath.substring (indexOfDot);

    return String::empty;
}

bool File::hasFileExtension (const String& possibleSuffix) const
{
    if (possibleSuffix.isEmpty())
        return fullPath.lastIndexOfChar ('.') <= fullPath.lastIndexOfChar (separator);

    const int semicolon = possibleSuffix.indexOfChar (0, ';');

    if (semicolon >= 0)
    {
        return hasFileExtension (possibleSuffix.substring (0, semicolon).trimEnd())
                || hasFileExtension (possibleSuffix.substring (semicolon + 1).trimStart());
    }
    else
    {
        if (fullPath.endsWithIgnoreCase (possibleSuffix))
        {
            if (possibleSuffix.startsWithChar ('.'))
                return true;

            const int dotPos = fullPath.length() - possibleSuffix.length() - 1;

            if (dotPos >= 0)
                return fullPath [dotPos] == '.';
        }
    }

    return false;
}

File File::withFileExtension (const String& newExtension) const
{
    if (fullPath.isEmpty())
        return File::nonexistent ();

    String filePart (getFileName());

    const int i = filePart.lastIndexOfChar ('.');
    if (i >= 0)
        filePart = filePart.substring (0, i);

    if (newExtension.isNotEmpty() && ! newExtension.startsWithChar ('.'))
        filePart << '.';

    return getSiblingFile (filePart + newExtension);
}

//==============================================================================
FileInputStream* File::createInputStream() const
{
    std::unique_ptr <FileInputStream> fin (new FileInputStream (*this));

    if (fin->openedOk())
        return fin.release();

    return nullptr;
}

FileOutputStream* File::createOutputStream (const size_t bufferSize) const
{
    std::unique_ptr <FileOutputStream> out (new FileOutputStream (*this, bufferSize));

    return out->failedToOpen() ? nullptr
                               : out.release();
}

//==============================================================================
bool File::appendData (const void* const dataToAppend,
                       const size_t numberOfBytes) const
{
    bassert (((std::ptrdiff_t) numberOfBytes) >= 0);

    if (numberOfBytes == 0)
        return true;

    FileOutputStream out (*this, 8192);
    return out.openedOk() && out.write (dataToAppend, numberOfBytes);
}

bool File::appendText (const String& text,
                       const bool asUnicode,
                       const bool writeUnicodeHeaderBytes) const
{
    FileOutputStream out (*this);

    if (out.failedToOpen())
        return false;

    out.writeText (text, asUnicode, writeUnicodeHeaderBytes);
    return true;
}

//==============================================================================
File File::createTempFile (const String& fileNameEnding)
{
    const File tempFile (getSpecialLocation (tempDirectory)
                            .getChildFile ("temp_" + String::toHexString (Random::getSystemRandom().nextInt()))
                            .withFileExtension (fileNameEnding));

    if (tempFile.exists())
        return createTempFile (fileNameEnding);

    return tempFile;
}

} // beast
