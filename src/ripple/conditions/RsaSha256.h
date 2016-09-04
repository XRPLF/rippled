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

#ifndef RIPPLE_CONDITIONS_RSA_SHA256_H
#define RIPPLE_CONDITIONS_RSA_SHA256_H

#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/basics/Buffer.h>
#include <cstdint>

namespace ripple {
namespace cryptoconditions {

class RsaSha256 final
    : public Fulfillment
{
    Buffer modulus_;
    Buffer signature_;

public:
    RsaSha256 () = default;

    Condition
    condition() const override;

    std::uint16_t
    type () const override
    {
        return condition_rsa_sha256;
    }

    std::uint32_t
    features () const override
    {
        return feature_rsa_pss | feature_sha256;
    }

    bool
    ok () const override
    {
        return !modulus_.empty() && !signature_.empty();
    }

    std::size_t
    payloadSize () const override;

    Buffer
    payload() const override;

    bool
    validate (Slice data) const override;

    /** Sign the given message with an RSA key */
    bool sign (
        std::string const& key,
        Slice message);

    bool
    parsePayload (Slice s) override;
};

}

}

#endif
