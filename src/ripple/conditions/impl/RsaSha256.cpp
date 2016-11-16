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

#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/RsaSha256.h>
#include <ripple/conditions/impl/base64.h>
#include <ripple/protocol/digest.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

#include <boost/algorithm/clamp.hpp>
#include <algorithm>
#include <iterator>

namespace ripple {
namespace cryptoconditions {

namespace detail {

struct rsa_deleter
{
    void operator() (RSA* rsa) const
    {
        RSA_free (rsa);
    }
};

using RsaKey = std::unique_ptr<RSA, rsa_deleter>;

struct bn_deleter
{
    void operator() (BIGNUM* bn) const
    {
        BN_free (bn);
    }
};

using BigNum = std::unique_ptr<BIGNUM, bn_deleter>;

// Check whether the public modulus meets the length
// requirements imposed by section 4.4.1 of the RFC.
bool
checkModulusLength (int len)
{
    if (len <= 0)
        return false;

    return len == boost::algorithm::clamp(len, 128, 512);
}

bool
signHelper (
    RSA* key,
    Slice message,
    Buffer& modulus,
    Buffer& signature)
{
    int const keySize = RSA_size(key);
    if (!checkModulusLength (keySize))
        return false;

    sha256_hasher h;
    h (message.data(), message.size());
    auto digest = static_cast<sha256_hasher::result_type>(h);

    Buffer buf;

    // Pad the result (-1 -> use hash length as salt length)
    if (!RSA_padding_add_PKCS1_PSS(key,
            buf.alloc(keySize), digest.data(),
            EVP_sha256(), -1))
        return false;

    // Sign - we've manually padded the input already.
    auto ret = RSA_private_encrypt(keySize, buf.data(),
        signature.alloc (buf.size()), key, RSA_NO_PADDING);

    if (ret == -1)
        return false;

    BN_bn2bin (key->n, modulus.alloc(BN_num_bytes (key->n)));
    return true;
}

bool
validateHelper (
    RSA* key,
    Slice message,
    Slice signature)
{
    int const keySize = RSA_size(key);
    if (!checkModulusLength (keySize))
        return false;

    Buffer buf;

    auto ret = RSA_public_decrypt(
        keySize,
        signature.data(),
        buf.alloc (keySize),
        key,
        RSA_NO_PADDING);

    if (ret == -1)
        return false;

    sha256_hasher h;
    h (message.data(), message.size());
    auto digest = static_cast<sha256_hasher::result_type>(h);

    return RSA_verify_PKCS1_PSS(key, digest.data(), EVP_sha256(), buf.data(), -1) == 1;
}

bool
parsePayloadHelper(
    Slice s,
    Buffer& modulus,
    Buffer& signature)
{
    auto start = s.data ();
    auto finish = s.data () + s.size();

    std::size_t len;

    std::tie (start, len) = oer::decode_length (
        start, finish);

    if (std::distance (start, finish) < len)
        return false;

    std::memcpy (modulus.alloc (len), start, len);
    std::advance (start, len);

    std::tie (start, len) = oer::decode_length (
        start, finish);

    if (std::distance (start, finish) < len)
        return false;

    std::memcpy (signature.alloc (len), start, len);
    std::advance (start, len);

    // Enforce constraints from the RFC:
    BigNum sig (BN_bin2bn (
        signature.data(), signature.size(), nullptr));

    BigNum mod (BN_bin2bn (
        modulus.data(), modulus.size(), nullptr));

    if (!sig || !mod)
        return false;

    // Per 4.4.1 of the RFC we are required to reject
    // moduli smaller than 128 bytes or greater than
    // 512 bytes.
    int modBytes = BN_num_bytes (mod.get());

    if (!checkModulusLength (modBytes))
        return false;

    // Per 4.4.2 of the RFC we must check whether the
    // signature and modulus consist of the same number
    // of octets and that the signature is numerically
    // less than the modulus:
    if (BN_num_bytes (sig.get()) != modBytes)
        return false;

    return BN_cmp (sig.get(), mod.get()) < 0;
}

}

Condition
RsaSha256::condition() const
{
    std::vector<std::uint8_t> m;
    m.reserve (1024);

    oer::encode_octetstring (
        modulus_.size(),
        modulus_.data(),
        modulus_.data() + modulus_.size(),
        std::back_inserter(m));

    sha256_hasher h;
    h (m.data(), m.size());

    Condition cc;
    cc.type = type();
    cc.featureBitmask = features();
    cc.maxFulfillmentLength = payloadSize();

    cc.fingerprint = static_cast<sha256_hasher::result_type>(h);

    return cc;
}


std::size_t
RsaSha256::payloadSize () const
{
    return
        oer::predict_octetstring_size (modulus_.size()) +
        oer::predict_octetstring_size (signature_.size());
}

Buffer
RsaSha256::payload() const
{
    Buffer b (payloadSize());

    auto out = oer::encode_octetstring (
        modulus_.size(),
        modulus_.data(),
        modulus_.data() + modulus_.size(),
        b.data());

    oer::encode_octetstring (
        signature_.size(),
        signature_.data(),
        signature_.data() + modulus_.size(),
        out);

    return b;
}

bool
RsaSha256::validate (Slice data) const
{
    if (!ok())
        return false;

    detail::RsaKey rsa (RSA_new());

    rsa->n = BN_new();
    BN_bin2bn(modulus_.data(), modulus_.size(), rsa->n);

    rsa->e = BN_new();
    BN_set_word (rsa->e, 65537);

    return detail::validateHelper (rsa.get(), data, signature_);
}

/** Sign the given message with an RSA key */
bool
RsaSha256::sign (
    std::string const& key,
    Slice message)
{
    // This ugly const_cast/reinterpret_cast is needed
    // on some machines. Although the documentation
    // suggests that the function accepts a void const*
    // argument, apparently some platforms have OpenSSL
    // libraries that are up-to-date but accept void*.
    auto bio = BIO_new_mem_buf(
        const_cast<void*>(static_cast<void const*>(key.data())),
        key.size());

    if (!bio)
        return false;

    detail::RsaKey rsa (PEM_read_bio_RSAPrivateKey(
        bio, NULL, NULL, NULL));

    BIO_free(bio);

    if (!rsa)
        return false;

    if (detail::signHelper (rsa.get(), message, modulus_, signature_))
        return true;

    modulus_.clear();
    signature_.clear();
    return false;
}

bool
RsaSha256::parsePayload (Slice s)
{
    // The payload may not be empty
    if (!s.empty() && detail::parsePayloadHelper (s, modulus_, signature_))
        return true;

    // Clear the state
    modulus_.clear();
    signature_.clear();
    return false;
}

}

}
