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

int64 beast_fileSetPosition (void* handle, int64 pos);

//==============================================================================
FileOutputStream::FileOutputStream (const File& f, const size_t bufferSizeToUse)
    : file (f),
      fileHandle (nullptr),
      status (Result::ok()),
      currentPosition (0),
      bufferSize (bufferSizeToUse),
      bytesInBuffer (0),
      buffer (bmax (bufferSizeToUse, (size_t) 16))
{
    openHandle();
}

FileOutputStream::~FileOutputStream()
{
    flushBuffer();
    flushInternal();
    closeHandle();
}

int64 FileOutputStream::getPosition()
{
    return currentPosition;
}

bool FileOutputStream::setPosition (int64 newPosition)
{
    if (newPosition != currentPosition)
    {
        flushBuffer();
        currentPosition = beast_fileSetPosition (fileHandle, newPosition);
    }

    return newPosition == currentPosition;
}

bool FileOutputStream::flushBuffer()
{
    bool ok = true;

    if (bytesInBuffer > 0)
    {
        ok = (writeInternal (buffer, bytesInBuffer) == (ssize_t) bytesInBuffer);
        bytesInBuffer = 0;
    }

    return ok;
}

void FileOutputStream::flush()
{
    flushBuffer();
    flushInternal();
}

bool FileOutputStream::write (const void* const src, const size_t numBytes)
{
    bassert (src != nullptr && ((ssize_t) numBytes) >= 0);

    if (bytesInBuffer + numBytes < bufferSize)
    {
        memcpy (buffer + bytesInBuffer, src, numBytes);
        bytesInBuffer += numBytes;
        currentPosition += numBytes;
    }
    else
    {
        if (! flushBuffer())
            return false;

        if (numBytes < bufferSize)
        {
            memcpy (buffer + bytesInBuffer, src, numBytes);
            bytesInBuffer += numBytes;
            currentPosition += numBytes;
        }
        else
        {
            const ssize_t bytesWritten = writeInternal (src, numBytes);

            if (bytesWritten < 0)
                return false;

            currentPosition += bytesWritten;
            return bytesWritten == (ssize_t) numBytes;
        }
    }

    return true;
}

bool FileOutputStream::writeRepeatedByte (uint8 byte, size_t numBytes)
{
    bassert (((ssize_t) numBytes) >= 0);

    if (bytesInBuffer + numBytes < bufferSize)
    {
        memset (buffer + bytesInBuffer, byte, numBytes);
        bytesInBuffer += numBytes;
        currentPosition += numBytes;
        return true;
    }

    return OutputStream::writeRepeatedByte (byte, numBytes);
}