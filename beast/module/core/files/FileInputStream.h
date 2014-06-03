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

#ifndef BEAST_FILEINPUTSTREAM_H_INCLUDED
#define BEAST_FILEINPUTSTREAM_H_INCLUDED

namespace beast
{

//==============================================================================
/**
    An input stream that reads from a local file.

    @see InputStream, FileOutputStream, File::createInputStream
*/
class FileInputStream
    : public InputStream
    , LeakChecked <FileInputStream>
{
public:
    //==============================================================================
    /** Creates a FileInputStream.

        @param fileToRead   the file to read from - if the file can't be accessed for some
                            reason, then the stream will just contain no data
    */
    explicit FileInputStream (const File& fileToRead);

    /** Destructor. */
    ~FileInputStream();

    //==============================================================================
    /** Returns the file that this stream is reading from. */
    const File& getFile() const noexcept                { return file; }

    /** Returns the status of the file stream.
        The result will be ok if the file opened successfully. If an error occurs while
        opening or reading from the file, this will contain an error message.
    */
    const Result& getStatus() const noexcept            { return status; }

    /** Returns true if the stream couldn't be opened for some reason.
        @see getResult()
    */
    bool failedToOpen() const noexcept                  { return status.failed(); }

    /** Returns true if the stream opened without problems.
        @see getResult()
    */
    bool openedOk() const noexcept                      { return status.wasOk(); }


    //==============================================================================
    std::int64_t getTotalLength();
    int read (void* destBuffer, int maxBytesToRead);
    bool isExhausted();
    std::int64_t getPosition();
    bool setPosition (std::int64_t pos);

private:
    //==============================================================================
    File file;
    void* fileHandle;
    std::int64_t currentPosition;
    Result status;
    bool needToSeek;

    void openHandle();
    void closeHandle();
    size_t readInternal (void* buffer, size_t numBytes);
};

} // beast

#endif   // BEAST_FILEINPUTSTREAM_H_INCLUDED
