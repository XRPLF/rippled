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

#ifndef BEAST_UTILITY_ERROR_H_INCLUDED
#define BEAST_UTILITY_ERROR_H_INCLUDED

#include <beast/Config.h>

#include <beast/strings/String.h>

#include <stdexcept>

namespace beast {

/** A concise error report.

    This lightweight but flexible class records lets you record the file and
    line where a recoverable error occurred, along with some optional human
    readable text.

    A recoverable error can be passed along and turned into a non recoverable
    error by throwing the object: it's derivation from std::exception is
    fully compliant with the C++ exception interface.

    @ingroup beast_core
*/
class Error
    : public std::exception
{
public:
    /** Numeric code.

        This enumeration is useful when the caller needs to take different
        actions depending on the failure. For example, trying again later if
        a file is locked.
    */
    enum Code
    {
        success,        //!< "the operation was successful"

        general,        //!< "a general error occurred"

        canceled,       //!< "the operation was canceled"
        exception,      //!< "an exception was thrown"
        unexpected,     //!< "an unexpected result was encountered"
        platform,       //!< "a system exception was signaled"

        noMemory,       //!< "there was not enough memory"
        noMoreData,     //!< "the end of data was reached"
        invalidData,    //!< "the data is corrupt or invalid"
        bufferSpace,    //!< "the buffer is too small"
        badParameter,   //!< "one or more parameters were invalid"
        assertFailed,   //!< "an assertion failed"

        fileInUse,      //!< "the file is in use"
        fileExists,     //!< "the file exists"
        fileNoPerm,     //!< "permission was denied" (file attributes conflict)
        fileIOError,    //!< "an I/O or device error occurred"
        fileNoSpace,    //!< "there is no space left on the device"
        fileNotFound,   //!< "the file was not found"
        fileNameInvalid //!< "the file name was illegal or malformed"
    };

    Error ();
    Error (Error const& other);
    Error& operator= (Error const& other);

    virtual ~Error () noexcept;

    Code code () const;
    bool failed () const;

    explicit operator bool () const
    {
        return code () != success;
    }

    String const getReasonText () const;
    String const getSourceFilename () const;
    int getLineNumber () const;

    Error& fail (char const* sourceFileName,
                 int lineNumber,
                 String const reasonText,
                 Code errorCode = general);

    Error& fail (char const* sourceFileName,
                 int lineNumber,
                 Code errorCode = general);

    // A function that is capable of recovering from an error (for
    // example, by performing a different action) can reset the
    // object so it can be passed up.
    void reset ();

    // Call this when reporting the error to clear the "checked" flag
    void willBeReported () const;

    // for std::exception. This lets you throw an Error that should
    // terminate the application. The what() message will be less
    // descriptive so ideally you should catch the Error object instead.
    char const* what () const noexcept;

    static String const getReasonTextForCode (Code code);

private:
    Code m_code;
    String m_reasonText;
    String m_sourceFileName;
    int m_lineNumber;
    mutable bool m_needsToBeChecked;
    mutable String m_what; // created on demand
    mutable char const* m_szWhat;
};

}

#endif
