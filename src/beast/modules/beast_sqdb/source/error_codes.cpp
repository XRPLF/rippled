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

    Portions based on SOCI - The C++ Database Access Library: 

    SOCI: http://soci.sourceforge.net/

    This file incorporates work covered by the following copyright
    and permission notice:

    Copyright (C) 2004 Maciej Sobczak, Stephen Hutton, Mateusz Loskot,
    Pawel Aleksander Fedorynski, David Courtney, Rafal Bobrowski,
    Julian Taylor, Henning Basold, Ilia Barahovski, Denis Arnaud,
    Daniel Lidstr�m, Matthieu Kermagoret, Artyom Beilis, Cory Bennett,
    Chris Weed, Michael Davidsaver, Jakub Stachowski, Alex Ott, Rainer Bauer,
    Martin Muenstermann, Philip Pemberton, Eli Green, Frederic Chateau,
    Artyom Tonkikh, Roger Orr, Robert Massaioli, Sergey Nikulov,
    Shridhar Daithankar, S�ren Meyer-Eppler, Mario Valesco.

    Boost Software License - Version 1.0, August 17th, 2003

    Permission is hereby granted, free of charge, to any person or organization
    obtaining a copy of the software and accompanying documentation covered by
    this license (the "Software") to use, reproduce, display, distribute,
    execute, and transmit the Software, and to prepare derivative works of the
    Software, and to permit third-parties to whom the Software is furnished to
    do so, all subject to the following:

    The copyright notices in the Software and this entire statement, including
    the above license grant, this restriction and the following disclaimer,
    must be included in all copies of the Software, in whole or in part, and
    all derivative works of the Software, unless such copies or derivative
    works are solely in the form of machine-executable object code generated by
    a source language processor.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
    SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
    FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/
//==============================================================================

namespace sqdb
{

namespace detail
{

#ifdef c
#undef c
#endif

const Error sqliteError(const char* sourceFileName,
                        int lineNumber,
                        int sqliteErrorCode)
{
    /*
    bassert (sqliteErrorCode != SQLITE_ROW &&
             sqliteErrorCode != SQLITE_DONE);
    */

    Error e;
    String s;
    Error::Code c;

    switch (sqliteErrorCode)
    {
        // should never get these two here but include them just in case
    case SQLITE_ROW:
    case SQLITE_DONE:

    case SQLITE_OK:
        // this is fine, and lets us avoid having to explicitly
        // check for SQLITE_OK and just deal in terms of the Error object.
        c = Error::success;
        break;

    case SQLITE_ERROR:
        s = TRANS("an sqlite error or missing database was encountered");
        c = Error::general;
        break;

    case SQLITE_INTERNAL:
        s = TRANS("sqlite encountered an internal logic error");
        c = Error::unexpected;
        break;

    case SQLITE_PERM:
        s = TRANS("sqlite was denied file access permission");
        c = Error::fileNoPerm;
        break;

    case SQLITE_ABORT:
        s = TRANS("the sqlite operation was canceled due to a callback");
        c = Error::canceled;
        break;

    case SQLITE_BUSY:
        s = TRANS("the sqlite database file is locked");
        c = Error::fileInUse;
        break;

    case SQLITE_LOCKED:
        s = TRANS("the sqlite database table was locked");
        c = Error::fileInUse;
        break;

    case SQLITE_NOMEM:
        s = TRANS("sqlite ran out of memory");
        c = Error::noMemory;
        break;

    case SQLITE_READONLY:
        s = TRANS("sqlite tried to write to a read-only database");
        c = Error::fileNoPerm;
        break;

    case SQLITE_INTERRUPT:
        s = TRANS("the sqlite operation was interrupted");
        c = Error::canceled;
        break;

    case SQLITE_IOERR:
        s = TRANS("sqlite encountered a device I/O error");
        c = Error::fileIOError;
        break;

    case SQLITE_CORRUPT:
        s = TRANS("the sqlite database is corrupt");
        c = Error::invalidData;
        break;

    case SQLITE_FULL:
        s = TRANS("the sqlite database is full");
        c = Error::fileNoSpace;
        break;

    case SQLITE_CANTOPEN:
        s = TRANS("the sqlite database could not be opened");
        c = Error::fileNotFound;
        break;

    case SQLITE_PROTOCOL:
        s = TRANS("sqlite encountered a lock protocol error");
        c = Error::badParameter;
        break;

    case SQLITE_EMPTY:
        s = TRANS("the sqlite database is empty");
        c = Error::noMoreData;
        break;

    case SQLITE_SCHEMA:
        s = TRANS("the sqlite database scheme was changed");
        c = Error::invalidData;
        break;

    case SQLITE_TOOBIG:
        s = TRANS("the sqlite string or blob was too large");
        c = Error::fileNoSpace;
        break;

    case SQLITE_CONSTRAINT:
        s = TRANS("the sqlite operation was aborted due to a constraint violation");
        c = Error::badParameter;
        break;

    case SQLITE_MISMATCH:
        s = TRANS("the sqlite data was mismatched");
        c = Error::badParameter;
        break;

    case SQLITE_MISUSE:
        s = TRANS("the sqlite library parameter was invalid");
        c = Error::badParameter;
        break;

    case SQLITE_NOLFS:
        s = TRANS("the sqlite platform feature is unavailable");
        c = Error::badParameter;
        break;

    case SQLITE_AUTH:
        s = TRANS("sqlite authorization was denied");
        c = Error::fileNoPerm;
        break;

    case SQLITE_FORMAT:
        s = TRANS("the auxiliary sqlite database has an invalid format");
        c = Error::invalidData;
        break;

    case SQLITE_RANGE:
        s = TRANS("the sqlite parameter was invalid");
        c = Error::badParameter;
        break;

    case SQLITE_NOTADB:
        s = TRANS("the file is not a sqlite database");
        c = Error::invalidData;
        break;

    default:
        s << TRANS("an unknown sqlite3 error code #")
          << String(sqliteErrorCode)
          << TRANS("was returned");
        c = Error::general;
        break;
    }

    if (c != Error::success)
        e.fail(sourceFileName, lineNumber, s, c);

    return e;
}

}

}
