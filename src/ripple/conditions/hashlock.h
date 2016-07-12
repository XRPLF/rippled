//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_CONDITIONS_HASHLOCK_H
#define RIPPLE_CONDITIONS_HASHLOCK_H

#include <ripple/basics/base_uint.h>
#include <ripple/conditions/condition.h>
#include <ripple/conditions/fulfillment.h>
#include <ripple/conditions/impl/base64.h>
#include <ripple/protocol/digest.h>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <iterator>
#include <vector>

namespace ripple {
namespace cryptoconditions {

struct hashlock_t final
    : public fulfillment_t
{
    Buffer payload_;

public:
    hashlock_t (std::vector<std::uint8_t> payload)
        : payload_ (payload.data(), payload.size())
    {
        if (payload_.size() > maxFulfillmentLength)
            throw std::length_error ("Maximum fulfillment length exceeded");
    }

    hashlock_t (Buffer payload)
        : payload_ (std::move(payload))
    {
        if (payload_.size() > maxFulfillmentLength)
            throw std::length_error ("Maximum fulfillment length exceeded");
    }

    hashlock_t (uint256 const& digest)
        : payload_ (digest.data(), digest.size())
    {
    }

    Buffer
    payload() const override
    {
        return { payload_.data(), payload_.size() };
    }

    condition_t
    condition() const override
    {
        sha256_hasher h;
        h (payload_.data(), payload_.size());

        condition_t cc;
        cc.type = type();
        cc.requires = requires();
        cc.fulfillment_length = payload_.size();
        cc.fingerprint = static_cast<sha256_hasher::result_type>(h);

        return cc;
    }

    std::uint16_t
    type () const override
    {
        return condition_hashlock;
    }

    std::uint32_t
    requires () const override
    {
        return feature_sha256 | feature_preimage;
    }

    bool
    validate (Slice const&) const override
    {
        // A fulfillment is always valid so there's nothing
        // to really check here - we just return true. This
        // may be counterintuitive at first, but the message
        // may have nothing to do with the preimage.

        return true;
    }
};

}

}

#endif
