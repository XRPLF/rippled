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

#ifndef BEAST_SUBREGIONSTREAM_H_INCLUDED
#define BEAST_SUBREGIONSTREAM_H_INCLUDED

//==============================================================================
/** Wraps another input stream, and reads from a specific part of it.

    This lets you take a subsection of a stream and present it as an entire
    stream in its own right.
*/
class BEAST_API SubregionStream
    : public InputStream
    , LeakChecked <SubregionStream>
{
public:
    //==============================================================================
    /** Creates a SubregionStream from an input source.

        @param sourceStream                 the source stream to read from
        @param startPositionInSourceStream  this is the position in the source stream that
                                            corresponds to position 0 in this stream
        @param lengthOfSourceStream         this specifies the maximum number of bytes
                                            from the source stream that will be passed through
                                            by this stream. When the position of this stream
                                            exceeds lengthOfSourceStream, it will cause an end-of-stream.
                                            If the length passed in here is greater than the length
                                            of the source stream (as returned by getTotalLength()),
                                            then the smaller value will be used.
                                            Passing a negative value for this parameter means it
                                            will keep reading until the source's end-of-stream.
        @param deleteSourceWhenDestroyed    whether the sourceStream that is passed in should be
                                            deleted by this object when it is itself deleted.
    */
    SubregionStream (InputStream* sourceStream,
                     int64 startPositionInSourceStream,
                     int64 lengthOfSourceStream,
                     bool deleteSourceWhenDestroyed);

    /** Destructor.

        This may also delete the source stream, if that option was chosen when the
        buffered stream was created.
    */
    ~SubregionStream();


    //==============================================================================
    int64 getTotalLength();
    int64 getPosition();
    bool setPosition (int64 newPosition);
    int read (void* destBuffer, int maxBytesToRead);
    bool isExhausted();


    //==============================================================================
private:
    OptionalScopedPointer<InputStream> source;
    const int64 startPositionInSourceStream, lengthOfSourceStream;
};

#endif   // BEAST_SUBREGIONSTREAM_H_INCLUDED
