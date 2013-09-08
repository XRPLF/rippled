//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_ASIO_BASICS_CONTENTBODYBUFFER_H_INCLUDED
#define BEAST_ASIO_BASICS_CONTENTBODYBUFFER_H_INCLUDED

/** Dynamic storage optimized for a large Content-Body of unknown size.
    This comes at the expense of discontiguous storage of the segments.
    We derive from SharedObject to make transfer of ownership inexpensive.
*/
class ContentBodyBuffer
{
public:
    enum
    {
        defaultBlocksize = 32 * 1024
    };

    typedef std::size_t     size_type;
    typedef ConstBuffers    const_buffers_tyoe;
    typedef MutableBuffers  mutable_buffers_type;

    explicit ContentBodyBuffer (size_type blocksize = defaultBlocksize);

    ~ContentBodyBuffer ();

    /** Swap the contents of this buffer with another.
        This is the preferred way to transfer ownership.
    */
    void swapWith (ContentBodyBuffer& other);

    /** Move bytes from the output to the input sequence.
        This will invalidate references to buffers.
    */
    void commit (size_type n);

    /** Returns a buffer to the input sequence. */
    ConstBuffers data () const;

    /** Returns the size of the input sequence. */
    size_type size () const;

    /** Reserve space in the output sequence.
        This also returns a buffer suitable for writing.
    */
    MutableBuffers prepare (size_type n);

    /** Reserve space in the output sequence. */
    void reserve (size_type n);

    /** Release memory while preserving the input sequence. */
    void shrink_to_fit ();

private:
    typedef std::vector <void*> Handles;

    size_type m_blocksize;
    size_type m_size;
    Handles m_handles;
};

#endif
