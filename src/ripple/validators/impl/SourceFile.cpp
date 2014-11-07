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

#include <ripple/validators/impl/Utilities.h>

#include <fstream>
#include <string>

namespace ripple {
namespace Validators {

class SourceFileImp
    : public SourceFile
    , public beast::LeakChecked <SourceFileImp>
{
public:
    SourceFileImp (beast::File const& file)
        : m_file (file)
    {
    }

    ~SourceFileImp ()
    {
    }

    std::string to_string () const
    {
        return "File: '" + m_file.getFullPathName().toStdString() + "'";
    }

    std::string uniqueID () const
    {
        return "File," + m_file.getFullPathName ().toStdString();
    }

    std::string createParam ()
    {
        return m_file.getFullPathName ().toStdString();
    }

    void fetch (Results& results, beast::Journal journal)
    {
        // 8MB is a somewhat arbitrary maximum file size, but it should be
        // enough to cover all cases in the foreseeable future.

        std::int64_t const maxFileSize = 8 * 1024 * 1024;
        std::int64_t const fileSize = m_file.getSize ();
        
        if (fileSize != 0 && (fileSize < maxFileSize))
        {
            std::ifstream datafile (m_file.getFullPathName().toStdString ());
            std::string line;

            if (datafile.is_open ())
            {
                while (std::getline(datafile, line))
                    Utilities::parseResultLine (results, line, journal);
            }
        }
        else
        {
            // file doesn't exist
        }
    }

private:
    beast::File m_file;
};

//------------------------------------------------------------------------------

SourceFile* SourceFile::New (beast::File const& file)
{
    return new SourceFileImp (file);
}

}
}
