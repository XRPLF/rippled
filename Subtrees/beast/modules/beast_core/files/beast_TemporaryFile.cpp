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

static File createTempFile (const File& parentDirectory, String name,
                            const String& suffix, const int optionFlags)
{
    if ((optionFlags & TemporaryFile::useHiddenFile) != 0)
        name = "." + name;

    return parentDirectory.getNonexistentChildFile (name, suffix, (optionFlags & TemporaryFile::putNumbersInBrackets) != 0);
}

TemporaryFile::TemporaryFile (const String& suffix, const int optionFlags)
    : temporaryFile (createTempFile (File::getSpecialLocation (File::tempDirectory),
                                     "temp_" + String::toHexString (Random::getSystemRandom().nextInt()),
                                     suffix, optionFlags))
{
}

TemporaryFile::TemporaryFile (const File& target, const int optionFlags)
    : temporaryFile (createTempFile (target.getParentDirectory(),
                                     target.getFileNameWithoutExtension()
                                       + "_temp" + String::toHexString (Random::getSystemRandom().nextInt()),
                                     target.getFileExtension(), optionFlags)),
      targetFile (target)
{
    // If you use this constructor, you need to give it a valid target file!
    bassert (targetFile != File::nonexistent ());
}

TemporaryFile::TemporaryFile (const File& target, const File& temporary)
    : temporaryFile (temporary), targetFile (target)
{
}

TemporaryFile::~TemporaryFile()
{
    if (! deleteTemporaryFile())
    {
        /* Failed to delete our temporary file! The most likely reason for this would be
           that you've not closed an output stream that was being used to write to file.

           If you find that something beyond your control is changing permissions on
           your temporary files and preventing them from being deleted, you may want to
           call TemporaryFile::deleteTemporaryFile() to detect those error cases and
           handle them appropriately.
        */
        bassertfalse;
    }
}

//==============================================================================
bool TemporaryFile::overwriteTargetFileWithTemporary() const
{
    // This method only works if you created this object with the constructor
    // that takes a target file!
    bassert (targetFile != File::nonexistent ());

    if (temporaryFile.exists())
    {
        // Have a few attempts at overwriting the file before giving up..
        for (int i = 5; --i >= 0;)
        {
            if (temporaryFile.moveFileTo (targetFile))
                return true;

            Thread::sleep (100);
        }
    }
    else
    {
        // There's no temporary file to use. If your write failed, you should
        // probably check, and not bother calling this method.
        bassertfalse;
    }

    return false;
}

bool TemporaryFile::deleteTemporaryFile() const
{
    // Have a few attempts at deleting the file before giving up..
    for (int i = 5; --i >= 0;)
    {
        if (temporaryFile.deleteFile())
            return true;

        Thread::sleep (50);
    }

    return false;
}
