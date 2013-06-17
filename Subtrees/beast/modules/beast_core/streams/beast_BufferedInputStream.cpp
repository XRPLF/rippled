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

namespace
{
    int calcBufferStreamBufferSize (int requestedSize, InputStream* const source) noexcept
    {
        // You need to supply a real stream when creating a BufferedInputStream
        bassert (source != nullptr);

        requestedSize = bmax (256, requestedSize);

        const int64 sourceSize = source->getTotalLength();
        if (sourceSize >= 0 && sourceSize < requestedSize)
            requestedSize = bmax (32, (int) sourceSize);

        return requestedSize;
    }
}

//==============================================================================
BufferedInputStream::BufferedInputStream (InputStream* const sourceStream, const int bufferSize_,
                                          const bool deleteSourceWhenDestroyed)
   : source (sourceStream, deleteSourceWhenDestroyed),
     bufferSize (calcBufferStreamBufferSize (bufferSize_, sourceStream)),
     position (sourceStream->getPosition()),
     lastReadPos (0),
     bufferStart (position),
     bufferOverlap (128)
{
    buffer.malloc ((size_t) bufferSize);
}

BufferedInputStream::BufferedInputStream (InputStream& sourceStream, const int bufferSize_)
   : source (&sourceStream, false),
     bufferSize (calcBufferStreamBufferSize (bufferSize_, &sourceStream)),
     position (sourceStream.getPosition()),
     lastReadPos (0),
     bufferStart (position),
     bufferOverlap (128)
{
    buffer.malloc ((size_t) bufferSize);
}

BufferedInputStream::~BufferedInputStream()
{
}

//==============================================================================
int64 BufferedInputStream::getTotalLength()
{
    return source->getTotalLength();
}

int64 BufferedInputStream::getPosition()
{
    return position;
}

bool BufferedInputStream::setPosition (int64 newPosition)
{
    position = bmax ((int64) 0, newPosition);
    return true;
}

bool BufferedInputStream::isExhausted()
{
    return position >= lastReadPos && source->isExhausted();
}

void BufferedInputStream::ensureBuffered()
{
    const int64 bufferEndOverlap = lastReadPos - bufferOverlap;

    if (position < bufferStart || position >= bufferEndOverlap)
    {
        int bytesRead;

        if (position < lastReadPos
             && position >= bufferEndOverlap
             && position >= bufferStart)
        {
            const int bytesToKeep = (int) (lastReadPos - position);
            memmove (buffer, buffer + (int) (position - bufferStart), (size_t) bytesToKeep);

            bufferStart = position;

            bytesRead = source->read (buffer + bytesToKeep,
                                      (int) (bufferSize - bytesToKeep));

            lastReadPos += bytesRead;
            bytesRead += bytesToKeep;
        }
        else
        {
            bufferStart = position;
            source->setPosition (bufferStart);
            bytesRead = source->read (buffer, bufferSize);
            lastReadPos = bufferStart + bytesRead;
        }

        while (bytesRead < bufferSize)
            buffer [bytesRead++] = 0;
    }
}

int BufferedInputStream::read (void* destBuffer, int maxBytesToRead)
{
    bassert (destBuffer != nullptr && maxBytesToRead >= 0);

    if (position >= bufferStart
         && position + maxBytesToRead <= lastReadPos)
    {
        memcpy (destBuffer, buffer + (int) (position - bufferStart), (size_t) maxBytesToRead);
        position += maxBytesToRead;

        return maxBytesToRead;
    }
    else
    {
        if (position < bufferStart || position >= lastReadPos)
            ensureBuffered();

        int bytesRead = 0;

        while (maxBytesToRead > 0)
        {
            const int bytesAvailable = bmin (maxBytesToRead, (int) (lastReadPos - position));

            if (bytesAvailable > 0)
            {
                memcpy (destBuffer, buffer + (int) (position - bufferStart), (size_t) bytesAvailable);
                maxBytesToRead -= bytesAvailable;
                bytesRead += bytesAvailable;
                position += bytesAvailable;
                destBuffer = static_cast <char*> (destBuffer) + bytesAvailable;
            }

            const int64 oldLastReadPos = lastReadPos;
            ensureBuffered();

            if (oldLastReadPos == lastReadPos)
                break; // if ensureBuffered() failed to read any more data, bail out

            if (isExhausted())
                break;
        }

        return bytesRead;
    }
}

String BufferedInputStream::readString()
{
    if (position >= bufferStart
         && position < lastReadPos)
    {
        const int maxChars = (int) (lastReadPos - position);

        const char* const src = buffer + (int) (position - bufferStart);

        for (int i = 0; i < maxChars; ++i)
        {
            if (src[i] == 0)
            {
                position += i + 1;
                return String::fromUTF8 (src, i);
            }
        }
    }

    return InputStream::readString();
}
