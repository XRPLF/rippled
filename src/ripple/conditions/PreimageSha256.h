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

#ifndef RIPPLE_CONDITIONS_PREIMAGE_SHA256_H
#define RIPPLE_CONDITIONS_PREIMAGE_SHA256_H

#include <ripple/basics/base_uint.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/base64.h>
#include <ripple/protocol/digest.h>
#include <algorithm>
#include <iterator>
#include <vector>

namespace ripple {
namespace cryptoconditions {

class PreimageSha256 final
    : public Fulfillment
{
    Buffer payload_;

public:
    PreimageSha256() = default;

    PreimageSha256 (Slice s)
        : payload_ (s)
    {
        // TODO: We don't want to throw. Devise better
        //       interface for constructing hashlock from
        //       given buffer.
        if (payload_.size() > maxSupportedFulfillmentLength)
            throw std::length_error (
                "Maximum fulfillment length exceeded");
    }

    std::size_t
    payloadSize () const override
    {
        return payload_.size();
    }

    Buffer
    payload() const override
    {
        return { payload_.data(), payload_.size() };
    }

    Condition
    condition() const override
    {
        sha256_hasher h;
        h (payload_.data(), payload_.size());

        Condition cc;
        cc.type = type();
        cc.featureBitmask = features();
        cc.maxFulfillmentLength = payload_.size();
        cc.fingerprint = static_cast<sha256_hasher::result_type>(h);

        return cc;
    }

    std::uint16_t
    type () const override
    {
        return condition_hashlock;
    }

    std::uint32_t
    features () const override
    {
        return feature_sha256 | feature_preimage;
    }

    bool
    ok () const override
    {
        return true;
    }

    bool
    validate (Slice) const override
    {
        // Perhaps counterintuitively, the message isn't
        // relevant.
        return true;
    }

    bool
    parsePayload (Slice s) override
    {
        // The payload may be empty
        if (s.size() > maxSupportedFulfillmentLength)
            return false;

        payload_ = s;
        return true;
    }
};

}

}

#endif
