/*
  ==============================================================================

   This file is part of the beast_core module of the BEAST library.
   Copyright (c) 2013 - Raw Material Software Ltd.

   Permission to use, copy, modify, and/or distribute this software for any purpose with
   or without fee is hereby granted, provided that the above copyright notice and this
   permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
   TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
   NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
   DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
   IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ------------------------------------------------------------------------------

   NOTE! This permissive ISC license applies ONLY to files within the beast_core module!
   All other BEAST modules are covered by a dual GPL/commercial license, so if you are
   using any other modules, be sure to check that you also comply with their license.

   For more details, visit www.beast.com

  ==============================================================================
*/

#ifndef BEAST_GZIPDECOMPRESSORINPUTSTREAM_H_INCLUDED
#define BEAST_GZIPDECOMPRESSORINPUTSTREAM_H_INCLUDED

namespace beast
{

//==============================================================================
/**
    This stream will decompress a source-stream using zlib.

    Tip: if you're reading lots of small items from one of these streams, you
         can increase the performance enormously by passing it through a
         BufferedInputStream, so that it has to read larger blocks less often.

    @see GZIPCompressorOutputStream
*/
class BEAST_API GZIPDecompressorInputStream
    : public InputStream
    , LeakChecked <GZIPDecompressorInputStream>
{
public:
    //==============================================================================
    /** Creates a decompressor stream.

        @param sourceStream                 the stream to read from
        @param deleteSourceWhenDestroyed    whether or not to delete the source stream
                                            when this object is destroyed
        @param noWrap                       this is used internally by the ZipFile class
                                            and should be ignored by user applications
        @param uncompressedStreamLength     if the creator knows the length that the
                                            uncompressed stream will be, then it can supply this
                                            value, which will be returned by getTotalLength()
    */
    GZIPDecompressorInputStream (InputStream* sourceStream,
                                 bool deleteSourceWhenDestroyed,
                                 bool noWrap = false,
                                 int64 uncompressedStreamLength = -1);

    /** Creates a decompressor stream.

        @param sourceStream     the stream to read from - the source stream must not be
                                deleted until this object has been destroyed
    */
    GZIPDecompressorInputStream (InputStream& sourceStream);

    /** Destructor. */
    ~GZIPDecompressorInputStream();

    //==============================================================================
    int64 getPosition();
    bool setPosition (int64 pos);
    int64 getTotalLength();
    bool isExhausted();
    int read (void* destBuffer, int maxBytesToRead);


    //==============================================================================
private:
    OptionalScopedPointer<InputStream> sourceStream;
    const int64 uncompressedStreamLength;
    const bool noWrap;
    bool isEof;
    int activeBufferSize;
    int64 originalSourcePos, currentPos;
    HeapBlock <uint8> buffer;

    class GZIPDecompressHelper;
    friend class ScopedPointer <GZIPDecompressHelper>;
    ScopedPointer <GZIPDecompressHelper> helper;
};

}  // namespace beast

#endif   // BEAST_GZIPDECOMPRESSORINPUTSTREAM_H_INCLUDED
