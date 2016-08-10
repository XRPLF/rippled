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

#ifndef RIPPLE_CONDITIONS_PREFIX_SHA256_H
#define RIPPLE_CONDITIONS_PREFIX_SHA256_H

#include <ripple/basics/base_uint.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/base64.h>
#include <ripple/protocol/digest.h>
#include <algorithm>
#include <iterator>
#include <memory>
#include <vector>

namespace ripple {
namespace cryptoconditions {

class PrefixSha256 final
    : public Fulfillment
{
    Buffer prefix_;
    std::unique_ptr<Fulfillment> subfulfillment_;

public:
    PrefixSha256 () = default;

    std::size_t
    payloadSize () const override
    {
        return
            oer::predict_octetstring_size(prefix_.size()) +
            subfulfillment_->payloadSize();
    }

    Buffer
    payload() const override
    {
        // We should never have a condition in a state that
        // isn't ok to call payload on.
        if (!ok())
            return {};

        auto const subpayload = to_blob (*subfulfillment_);

        Buffer b (subpayload.size() +
            oer::predict_octetstring_size (prefix_.size()));

        auto out = oer::encode_octetstring (
            prefix_.size(),
            prefix_.data(),
            prefix_.data() + prefix_.size(),
            b.data());

        std::memcpy (out, subpayload.data(), subpayload.size());

        return b;
    }

    Condition
    condition() const override
    {
        auto const sc = subcondition();
        auto const blob = to_blob (sc);

        Buffer b (blob.size() +
            oer::predict_octetstring_size (prefix_.size()));

        auto out = oer::encode_octetstring (
            prefix_.size(),
            prefix_.data(),
            prefix_.data() + prefix_.size(),
            b.data());

        std::memcpy (out, blob.data(), blob.size());

        sha256_hasher h;
        h (b.data(), b.size());

        Condition cc;
        cc.type = type();
        cc.featureBitmask = features();
        cc.maxFulfillmentLength = payloadSize();
        cc.fingerprint = static_cast<sha256_hasher::result_type>(h);

        return cc;
    }

    std::uint16_t
    type () const override
    {
        return condition_prefix_sha256;
    }

    std::uint32_t
    features () const override
    {
        return
            feature_sha256 |
            feature_prefix |
            subfulfillment_->features();
    }

    bool
    ok () const override
    {
        return static_cast<bool>(subfulfillment_);
    }

    bool
    validate (Slice m) const override
    {
        if (!ok())
            return false;

        // Prepend the prefix to the message:
        Buffer b (prefix_.size() + m.size());

        if (prefix_.size())
            std::memcpy (b.data(), prefix_.data(), prefix_.size());

        if (m.size())
            std::memcpy (b.data() + prefix_.size(), m.data(), m.size());

        return subfulfillment_->validate (b);
    }

    Fulfillment const&
    subfulfillment () const
    {
        return *subfulfillment_;
    }

    Condition
    subcondition () const
    {
        return subfulfillment_->condition();
    }

    bool
    parsePayload (Slice s) override
    {
        // The payload consists of the prefix, followed by
        // a subfulfillment. It cannot be empty:
        if (s.empty())
            return false;

        auto start = s.data();
        auto finish = s.data() + s.size();

        std::size_t len;

        std::tie (start, len) = oer::decode_length (
            start, finish);

        if (len != 0)
        {
            if (std::distance (start, finish) < len)
                return false;

            std::memcpy (prefix_.alloc (len), start, len);
            std::advance (start, len);
        }

        s += std::distance (s.data(), start);

        // The remaining bytes in the slice are a fulfillment
        // so we parse it as such. If we can, then we've
        // succeeded.
        subfulfillment_ = loadFulfillment (s);

        if (!subfulfillment_)
        {
            prefix_.clear();
            return false;
        }

        return true;
    }

    void setPrefix (Slice prefix)
    {
        prefix_ = prefix;
    }

    Slice prefix() const
    {
        return prefix_;
    }

    void setSubfulfillment (std::unique_ptr<Fulfillment> subfulfillment)
    {
        subfulfillment_ = std::move (subfulfillment);
    }
};

}

}

#endif
