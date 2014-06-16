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

#include <beast/cxx14/memory.h> // <memory>
#include <fstream>
    
namespace ripple {

LogFile::LogFile ()
    : m_stream (nullptr)
{
}

LogFile::~LogFile ()
{
}

bool LogFile::isOpen () const noexcept
{
    return m_stream != nullptr;
}

bool LogFile::open (boost::filesystem::path const& path)
{
    close ();

    bool wasOpened = false;

    // VFALCO TODO Make this work with Unicode file paths
    std::unique_ptr <std::ofstream> stream (
        new std::ofstream (path.c_str (), std::fstream::app));

    if (stream->good ())
    {
        m_path = path;

        m_stream = std::move (stream);

        wasOpened = true;
    }

    return wasOpened;
}

bool LogFile::closeAndReopen ()
{
    close ();

    return open (m_path);
}

void LogFile::close ()
{
    m_stream = nullptr;
}

void LogFile::write (char const* text)
{
    if (m_stream != nullptr)
        (*m_stream) << text;
}

void LogFile::writeln (char const* text)
{
    if (m_stream != nullptr)
    {
        (*m_stream) << text;
        (*m_stream) << std::endl;
    }
}

} // ripple
