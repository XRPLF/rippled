//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

namespace ripple {
namespace Validators {

class SourceFileImp
    : public SourceFile
    , public LeakChecked <SourceFileImp>
{
public:
    SourceFileImp (File const& file)
        : m_file (file)
    {
    }

    ~SourceFileImp ()
    {
    }
    
    String name ()
    {
        return "File :'" + m_file.getFullPathName () + "'";
    }

    String uniqueID ()
    {
        return "File," + m_file.getFullPathName ();
    }

    String createParam ()
    {
        return m_file.getFullPathName ();
    }

    Result fetch (Journal journal)
    {
        Result result;

        int64 const fileSize (m_file.getSize ());

        if (fileSize != 0)
        {
            if (fileSize < std::numeric_limits<int32>::max())
            {
                MemoryBlock buffer (fileSize);
                RandomAccessFile f;
                RandomAccessFile::ByteCount amountRead;

                f.open (m_file, RandomAccessFile::readOnly);
                f.read (buffer.begin(), fileSize, &amountRead);

                if (amountRead == fileSize)
                {
                }
            }
            else
            {
                // too big!
            }
        }
        else
        {
            // file doesn't exist
        }

        return result;
    }

private:
    File m_file;
};

//------------------------------------------------------------------------------

SourceFile* SourceFile::New (File const& file)
{
    ScopedPointer <SourceFile> object (
        new SourceFileImp (file));

    return object.release ();
}

}
}
