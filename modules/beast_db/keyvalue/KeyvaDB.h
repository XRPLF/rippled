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

#ifndef BEAST_KEYVADB_H_INCLUDED
#define BEAST_KEYVADB_H_INCLUDED

/** Specialized Key/value database

    Once written, a value can never be modified.
*/
class KeyvaDB : LeakChecked <KeyvaDB>
{
public:
    class GetCallback
    {
    public:
        virtual void* getStorageForValue (int valueBytes) = 0;
    };

    static KeyvaDB* New (int keyBytes,
                         int keyBlockDepth,
                         File keyPath,
                         File valPath);

    virtual ~KeyvaDB () { }

    // VFALCO TODO Make the return value a Result so we can
    //             detect corruption and errors!
    //
    virtual bool get (void const* key, GetCallback* callback) = 0;

    // VFALCO TODO Use Result for return value
    //
    virtual void put (void const* key, void const* value, int valueBytes) = 0;

    virtual void flush () = 0;
};

#endif
