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

#ifndef BEAST_RANDOMACCESSFILE_H_INCLUDED
#define BEAST_RANDOMACCESSFILE_H_INCLUDED

namespace beast
{

/** Provides random access reading and writing to an operating system file.

    This class wraps the underlying native operating system routines for
    opening and closing a file for reading and/or writing, seeking within
    the file, and performing read and write operations. There are also methods
    provided for obtaining an input or output stream which will work with
    the file.

    @note All files are opened in binary mode. No text newline conversions
          are performed.

    @note None of these members are thread safe. The caller is responsible
          for synchronization.

    @see FileInputStream, FileOutputStream
*/
class  RandomAccessFile : public Uncopyable, LeakChecked <RandomAccessFile>
{
public:
    /** The type of an FileOffset.

        This can be useful when writing templates.
    */
    typedef std::int64_t FileOffset;

    /** The type of a byte count.

        This can be useful when writing templates.
    */
    typedef size_t ByteCount;

    /** The access mode.

        @see open
    */
    enum Mode
    {
        readOnly,
        readWrite
    };

    //==============================================================================
    /** Creates an unopened file object.

        @see open, isOpen
    */
    RandomAccessFile () noexcept;

    /** Destroy the file object.

        If the operating system file is open it will be closed.
    */
    ~RandomAccessFile ();

    /** Determine if a file is open.

        @return `true` if the operating system file is open.
    */
    bool isOpen () const noexcept { return fileHandle != nullptr; }

    /** Opens a file object.

        The file is opened with the specified permissions. The initial
        position is set to the beginning of the file.

        @note If a file is already open, it will be closed first.

        @param path The path to the file
        @param mode The access permissions
        @return An indication of the success of the operation.

        @see Mode
    */
    Result open (File const& path, Mode mode);

    /** Closes the file object.

        Any data that needs to be flushed will be written before the file is closed.

        @note If no file is opened, this call does nothing.
    */
    void close ();

    /** Retrieve the @ref File associated with this object.

        @return The associated @ref File.
    */
    File const& getFile () const noexcept { return file; }

    /** Get the current position.

        The next read or write will take place from here.

        @return The current position, as an absolute byte FileOffset from the begining.
    */
    FileOffset getPosition () const noexcept { return currentPosition; }

    /** Set the current position.

        The next read or write will take place at this location.

        @param newPosition The byte FileOffset from the beginning of the file to move to.

        @return `true` if the operation was successful.
    */
    Result setPosition (FileOffset newPosition);

    /** Read data at the current position.

        The caller is responsible for making sure that the memory pointed to
        by `buffer` is at least as large as `bytesToRead`.

        @note The file must have been opened with read permission.

        @param buffer The memory to store the incoming data
        @param numBytes The number of bytes to read.
        @param pActualAmount Pointer to store the actual amount read, or `nullptr`.

        @return `true` if all the bytes were read.
    */
    Result read (void* buffer, ByteCount numBytes, ByteCount* pActualAmount = 0);

    /** Write data at the current position.

        The current position is advanced past the data written. If data is
        written past the end of the file, the file size is increased on disk.

        The caller is responsible for making sure that the memory pointed to
        by `buffer` is at least as large as `bytesToWrite`.

        @note The file must have been opened with write permission.

        @param data A pointer to the data buffer to write to the file.
        @param numBytes The number of bytes to write.
        @param pActualAmount Pointer to store the actual amount written, or `nullptr`.

        @return `true` if all the data was written.
    */
    Result write (const void* data, ByteCount numBytes, ByteCount* pActualAmount = 0);

    /** Truncate the file at the current position.
    */
    Result truncate ();

    /** Flush the output buffers.

        This calls the operating system to make sure all data has been written.
    */
    Result flush();

    //==============================================================================
private:
    // Some of these these methods are implemented natively on
    // the corresponding platform.
    //
    // See beast_posix_SharedCode.h and beast_win32_Files.cpp
    //
    Result nativeOpen (File const& path, Mode mode);
    void nativeClose ();
    Result nativeSetPosition (FileOffset newPosition);
    Result nativeRead (void* buffer, ByteCount numBytes, ByteCount* pActualAmount = 0);
    Result nativeWrite (const void* data, ByteCount numBytes, ByteCount* pActualAmount = 0);
    Result nativeTruncate ();
    Result nativeFlush ();

private:
    File file;
    void* fileHandle;
    FileOffset currentPosition;
};

} // beast

#endif

