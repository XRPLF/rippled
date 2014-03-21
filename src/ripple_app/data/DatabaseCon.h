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

#ifndef RIPPLE_DATABASECON_H
#define RIPPLE_DATABASECON_H

namespace ripple {

// VFALCO NOTE This looks like a pointless class. Figure out
//         what purpose it is really trying to serve and do it better.
class DatabaseCon : beast::LeakChecked <DatabaseCon>
{
public:
    DatabaseCon (const std::string& name, const char* initString[], int countInit);
    ~DatabaseCon ();
    Database* getDB ()
    {
        return mDatabase;
    }
    DeprecatedRecursiveMutex& getDBLock ()
    {
        return mLock;
    }

    // VFALCO TODO change "protected" to "private" throughout the code
private:
    Database*               mDatabase;
    DeprecatedRecursiveMutex  mLock;
};

} // ripple

#endif

