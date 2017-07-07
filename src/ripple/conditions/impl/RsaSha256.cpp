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

#include <ripple/conditions/impl/RsaSha256.h>
#include <ripple/protocol/digest.h>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include <boost/algorithm/clamp.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>

namespace ripple {
namespace cryptoconditions {

namespace detail {

struct rsa_deleter
{
    void
    operator()(RSA* rsa) const
    {
        RSA_free(rsa);
    }
};

using RsaKey = std::unique_ptr<RSA, rsa_deleter>;

struct bn_deleter
{
    void
    operator()(BIGNUM* bn) const
    {
        BN_free(bn);
    }
};

using BigNum = std::unique_ptr<BIGNUM, bn_deleter>;

// Check whether the public modulus meets the length
// requirements imposed by section 4.4.1 of the RFC.
bool
checkModulusLength(int len)
{
    if (len <= 0)
        return false;

    return len == boost::algorithm::clamp(len, 128, 512);
}

bool
validateHelper(RSA* key, Slice message, Slice signature)
{
    int const keySize = RSA_size(key);
    if (!checkModulusLength(keySize))
        return false;

    Buffer buf;

    auto ret = RSA_public_decrypt(
        keySize, signature.data(), buf.alloc(keySize), key, RSA_NO_PADDING);

    if (ret == -1)
        return false;

    sha256_hasher h;
    h(message.data(), message.size());
    auto digest = static_cast<sha256_hasher::result_type>(h);

    return RSA_verify_PKCS1_PSS(
               key, digest.data(), EVP_sha256(), buf.data(), -1) == 1;
}
}

RsaSha256::RsaSha256(Slice m, Slice s)
{
    modulus_.resize(m.size());
    std::copy(m.data(), m.data() + m.size(), modulus_.data());

    signature_.resize(s.size());
    std::copy(s.data(), s.data() + s.size(), signature_.data());
}

std::array<std::uint8_t, 32>
RsaSha256::fingerprint(std::error_code& ec) const
{
    return Fulfillment::fingerprint(ec);
}

void
RsaSha256::encodeFingerprint(der::Encoder& encoder) const
{
   // modulus must be greater than 128 bytes and less than or equal to 512 bytes
    encoder << std::tie(modulus_);
}

bool
RsaSha256::validate(Slice data) const
{
    if (modulus_.empty() || signature_.empty())
        return false;

    detail::RsaKey rsa(RSA_new());

    rsa->n = BN_new();
    BN_bin2bn(modulus_.data(), modulus_.size(), rsa->n);

    rsa->e = BN_new();
    BN_set_word(rsa->e, 65537);

    return detail::validateHelper(rsa.get(), data, makeSlice(signature_));
}

std::uint32_t
RsaSha256::cost() const
{
    auto const mSize = modulus_.size();
    if (mSize >= 65535)
        // would overflow
        return std::numeric_limits<std::uint32_t>::max();
    return mSize * mSize;
}

std::bitset<5>
RsaSha256::subtypes() const
{
    return std::bitset<5>{};
}

std::uint64_t
RsaSha256::derEncodedLength(
    boost::optional<der::GroupType> const& parentGroupType,
    der::TagMode encoderTagMode,
    der::TraitsCache& traitsCache) const
{
    return cryptoconditions::der::withTupleEncodedLengthHelper(
        *this, parentGroupType, encoderTagMode, traitsCache);
}

void
RsaSha256::encode(cryptoconditions::der::Encoder& encoder) const
{
    // modulus must be greater than 128 bytes and less than or equal to 512 bytes
    if (modulus_.size() <= 128 || modulus_.size() > 512)
    {
        encoder.ec_ =
            der::make_error_code(der::Error::rsaModulusSizeRangeError);
        return;
    }
    cryptoconditions::der::withTupleEncodeHelper(*this, encoder);
}

void
RsaSha256::decode(cryptoconditions::der::Decoder& decoder)
{
    cryptoconditions::der::withTupleDecodeHelper(*this, decoder);
    // modulus must be greater than 128 bytes and less than or equal to 512 bytes
    if (modulus_.size() <= 128 || modulus_.size() > 512)
    {
        decoder.ec_ =
            der::make_error_code(der::Error::rsaModulusSizeRangeError);
        return;
    }
}

bool
RsaSha256::checkEqualForTesting(Fulfillment const& rhs) const
{
    if (auto c = dynamic_cast<RsaSha256 const*>(&rhs))
        return c->modulus_ == modulus_ && c->signature_ == signature_;
    return false;

}

int
RsaSha256::compare(Fulfillment const& rhs, der::TraitsCache& traitsCache) const
{
    return cryptoconditions::der::withTupleCompareHelper(*this, rhs, traitsCache);
}


bool
RsaSha256::validationDependsOnMessage() const
{
    return true;
}

}
}
