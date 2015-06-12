//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_FIELDREADER_H_INCLUDED
#define RIPPLE_RPC_FIELDREADER_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>

namespace ripple {
namespace RPC {

class Context;

/** A FieldReader and its associated free functions are used to read parameters
    from an RPC request.

    The classes that can be read are:
      * bool
      * std::string
      * std::vector <std::string>
      * Account
      * std::set <Account>

    Additionally, there are free functions to read compound types like Ledger
    or RippleAddress.
 */
struct FieldReader
{
    Context const& context;
    Json::Value error;

    FieldReader (Context const& context_) : context(context_)
    {}
};

/** Read a required field of type T from a FieldReader.

    Return true on success;  on failure, fill FieldReader::error and
    return false.
 */
template <class T>
bool readRequired (FieldReader&, T& result, Json::StaticString fieldname);


/** Read an optional field of type T from a FieldReader.

    Return true on success, or if the field was missing; on failure, fill
    FieldReader::error and return false.
 */
template <class T>
bool readOptional (FieldReader&, T& result, Json::StaticString fieldName);

bool readLedger (FieldReader&, Ledger::pointer& result);
bool readAccount (FieldReader&, Account& result, std::string const& name);
bool readAccountAddress (FieldReader&, RippleAddress&);

////////////////////////////////////////////////////////////////////////////////
// Implementation details follow.

/** A request for a specific field follows. */
struct FieldRequest {
    FieldReader& reader;
    Json::StaticString field;
    Json::Value const& value;
};

/**
   This are the implementations of field reading for specific classes.  For a
   new class to be readable, there must be an implementation of readImpl -
   note that name-dependent lookup will work.
 */

void readImpl (bool&, FieldRequest const&);
void readImpl (std::string&, FieldRequest const&);
void readImpl (Account&, FieldRequest const&);
void readImpl (std::vector <std::string>&, FieldRequest const&);
void readImpl (std::set <Account>&, FieldRequest const&);

template <class T>
bool readRequired (FieldReader& reader, T& result, Json::StaticString field)
{
    auto& value = reader.context.params[field];
    if (! value)
        reader.error = missing_field_error (field);
    else
        readImpl (result, {reader, field, value});
    return reader.error.empty();
}

template <class T>
bool readOptional (FieldReader& reader, T& result, Json::StaticString field)
{
    auto& value = reader.context.params[field];
    if (! value)
        return true;

    readImpl (result, {reader, field, value});
    return reader.error.empty();
}

} // RPC
} // ripple

#endif
