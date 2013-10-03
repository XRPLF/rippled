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

#ifndef RIPPLE_VALIDATORS_STORE_H_INCLUDED
#define RIPPLE_VALIDATORS_STORE_H_INCLUDED

namespace ripple {
namespace Validators {

/** Abstract persistence for Validators data. */
class Store
{
public:
    virtual ~Store () { }

    /** Insert a new SourceDesc to the Store.
        The caller's SourceDesc will have any available persistent
        information filled in from the Store.
    */
    virtual void insert (SourceDesc& desc) = 0;
    
    /** Update the SourceDesc fixed fields.  */
    virtual void update (SourceDesc& desc, bool updateFetchResults = false) = 0;

protected:
    Store () { }
};

}
}

#endif
