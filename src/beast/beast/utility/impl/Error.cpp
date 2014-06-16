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

#include <beast/utility/Error.h>
#include <beast/utility/Debug.h>

#include <ostream>

// VFALCO TODO Localizable strings
#ifndef TRANS
#define TRANS(s) (s)
#define UNDEF_TRANS
#endif

namespace beast {

Error::Error ()
    : m_code (success)
    , m_lineNumber (0)
    , m_needsToBeChecked (true)
    , m_szWhat (0)
{
}

Error::Error (Error const& other)
    : m_code (other.m_code)
    , m_reasonText (other.m_reasonText)
    , m_sourceFileName (other.m_sourceFileName)
    , m_lineNumber (other.m_lineNumber)
    , m_needsToBeChecked (true)
    , m_szWhat (0)
{
    other.m_needsToBeChecked = false;
}

Error::~Error () noexcept
{
    /* If this goes off it means an error object was created but never tested */
    bassert (!m_needsToBeChecked);
}

Error& Error::operator= (Error const& other)
{
    m_code = other.m_code;
    m_reasonText = other.m_reasonText;
    m_sourceFileName = other.m_sourceFileName;
    m_lineNumber = other.m_lineNumber;
    m_needsToBeChecked = true;
    m_what = String::empty;
    m_szWhat = 0;

    other.m_needsToBeChecked = false;

    return *this;
}

Error::Code Error::code () const
{
    m_needsToBeChecked = false;
    return m_code;
}

bool Error::failed () const
{
    return code () != success;
}

String const Error::getReasonText () const
{
    return m_reasonText;
}

String const Error::getSourceFilename () const
{
    return m_sourceFileName;
}

int Error::getLineNumber () const
{
    return m_lineNumber;
}

Error& Error::fail (char const* sourceFileName,
                    int lineNumber,
                    String const reasonText,
                    Code errorCode)
{
    bassert (m_code == success);
    bassert (errorCode != success);

    m_code = errorCode;
    m_reasonText = reasonText;
    m_sourceFileName = Debug::getFileNameFromPath (sourceFileName);
    m_lineNumber = lineNumber;
    m_needsToBeChecked = true;

    return *this;
}

Error& Error::fail (char const* sourceFileName,
                    int lineNumber,
                    Code errorCode)
{
    return fail (sourceFileName,
                 lineNumber,
                 getReasonTextForCode (errorCode),
                 errorCode);
}

void Error::reset ()
{
    m_code = success;
    m_reasonText = String::empty;
    m_sourceFileName = String::empty;
    m_lineNumber = 0;
    m_needsToBeChecked = true;
    m_what = String::empty;
    m_szWhat = 0;
}

void Error::willBeReported () const
{
    m_needsToBeChecked = false;
}

char const* Error::what () const noexcept
{
    if (! m_szWhat)
    {
        // The application could not be initialized because sqlite was denied access permission
        // The application unexpectedly quit because the exception 'sqlite was denied access permission at file ' was thrown
        m_what <<
               m_reasonText << " " <<
               TRANS ("at file") << " '" <<
               m_sourceFileName << "' " <<
               TRANS ("line") << " " <<
               String (m_lineNumber) << " " <<
               TRANS ("with code") << " = " <<
               String (m_code);

        m_szWhat = (const char*)m_what.toUTF8 ();
    }

    return m_szWhat;
}

String const Error::getReasonTextForCode (Code code)
{
    String s;

    switch (code)
    {
    case success:
        s = TRANS ("the operation was successful");
        break;

    case general:
        s = TRANS ("a general error occurred");
        break;

    case canceled:
        s = TRANS ("the operation was canceled");
        break;

    case exception:
        s = TRANS ("an exception was thrown");
        break;

    case unexpected:
        s = TRANS ("an unexpected result was encountered");
        break;

    case platform:
        s = TRANS ("a system exception was signaled");
        break;

    case noMemory:
        s = TRANS ("there was not enough memory");
        break;

    case noMoreData:
        s = TRANS ("the end of data was reached");
        break;

    case invalidData:
        s = TRANS ("the data is corrupt or invalid");
        break;

    case bufferSpace:
        s = TRANS ("the buffer is too small");
        break;

    case badParameter:
        s = TRANS ("one or more parameters were invalid");
        break;

    case assertFailed:
        s = TRANS ("an assertion failed");
        break;

    case fileInUse:
        s = TRANS ("the file is in use");
        break;

    case fileExists:
        s = TRANS ("the file exists");
        break;

    case fileNoPerm:
        s = TRANS ("permission was denied");
        break;

    case fileIOError:
        s = TRANS ("an I/O or device error occurred");
        break;

    case fileNoSpace:
        s = TRANS ("there is no space left on the device");
        break;

    case fileNotFound:
        s = TRANS ("the file was not found");
        break;

    case fileNameInvalid:
        s = TRANS ("the file name was illegal or malformed");
        break;

    default:
        s = TRANS ("an unknown error code was received");
        break;
    }

    return s;
}

#ifdef UNDEF_TRANS
#undef TRANs
#undef UNDEF_TRANS
#endif

}

