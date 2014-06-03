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

#ifndef RIPPLE_VALIDATORS_SOURCE_H_INCLUDED
#define RIPPLE_VALIDATORS_SOURCE_H_INCLUDED

#include <beast/smart_ptr/SharedObject.h>
#include <beast/module/core/time/Time.h>

namespace ripple {
namespace Validators {

/** A source of validator descriptors. */
class Source : public beast::SharedObject
{
public:
    /** A Source's descriptor for a Validator. */
    struct Item
    {
        /** The unique key for this validator. */
        RipplePublicKey publicKey;

        /** Optional human readable comment describing the validator. */
        beast::String label;
    };

    /** Destroy the Source.
        This can be called from any thread. If the Source is busy
        fetching, the destructor must block until the operation is either
        canceled or complete.
    */
    virtual ~Source () { }

    /** The name of the source, used in diagnostic output. */
    virtual std::string to_string () const = 0;

    /** An identifier that uniquely describes the source.
        This is used for identification in the database.
    */
    virtual beast::String uniqueID () const = 0;

    /** A string that is used to recreate the source from the database entry. */
    virtual beast::String createParam () = 0;

    /** Cancel any pending fetch.
        The default implementation does nothing.
    */
    virtual void cancel () { }

    /** Fetch results.
        This call will block
    */
    /** @{ */
    struct Results
    {
        Results ();

        bool success;
        // VFALCO TODO Replace with std::string
        beast::String message;
        // VFALCO TODO Replace with chrono
        beast::Time expirationTime;
        std::vector <Item> list;
    };
    virtual void fetch (Results& results, beast::Journal journal) = 0;
    /** @} */
};

std::ostream& operator<< (std::ostream& os, Source const& v);

}
}

#endif
