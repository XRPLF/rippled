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

#include <BeastConfig.h>
#include <ripple/crypto/ECDSACanonical.h>
#include <beast/unit_test/suite.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace ripple {

namespace detail {

// A simple wrapper for a BIGNUM to make it
// easier to allocate, construct, and free them
struct BigNum
{
    BIGNUM* num;

    BigNum& operator=(BigNum const&) = delete;

    BigNum ()
        : num (BN_new ())
    {

    }

    BigNum (const char *hex)
        : num (BN_new ())
    {
        BN_hex2bn (&num, hex);
    }

    BigNum (unsigned char const* ptr, std::size_t len)
        : num (BN_new ())
    {
        set (ptr, len);
    }

    BigNum (BigNum const& other)
        : num (BN_new ())
    {
        if (BN_copy (num, other.num) == nullptr)
            BN_clear (num);
    }

    ~BigNum ()
    {
        BN_free (num);
    }

    operator BIGNUM* ()
    {
        return num;
    }

    operator BIGNUM const* () const
    {
        return num;
    }

    bool set (unsigned char const* ptr, std::size_t len)
    {
        if (BN_bin2bn (ptr, len, num) == nullptr)
            return false;

        return true;
    }
};

class SignaturePart
{
private:
    std::size_t m_skip;
    BigNum m_bn;

public:
    SignaturePart (unsigned char const* sig, std::size_t size)
        : m_skip (0)
    {
        // The format is: <02> <length of signature> <signature>
        if ((size < 3) || (sig[0] != 0x02))
            return;

        std::size_t const len (sig[1]);

        // Claimed length can't be longer than amount of data available
        if (len > (size - 2))
            return;

        // Signature must be between 1 and 33 bytes.
        if ((len < 1) || (len > 33))
            return;

        // The signature can't be negative
        if ((sig[2] & 0x80) != 0)
            return;

        // It can't be zero
        if ((sig[2] == 0) && (len == 1))
            return;

        // And it can't be padded
        if ((sig[2] == 0) && ((sig[3] & 0x80) == 0))
            return;

        // Load the signature (ignore the marker prefix and length) and if
        // successful, count the number of bytes we consumed.
        if (m_bn.set (sig + 2, len))
            m_skip = len + 2;
    }

    bool valid () const
    {
        return m_skip != 0;
    }

    // The signature as a BIGNUM
    BigNum getBigNum () const
    {
        return m_bn;
    }

    // Returns the number of bytes to skip for this signature part
    std::size_t skip () const
    {
        return m_skip;
    }
};

// The SECp256k1 modulus
static BigNum const modulus (
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");

} // detail

/** Determine whether a signature is canonical.
    Canonical signatures are important to protect against signature morphing
    attacks.
    @param vSig the signature data
    @param sigLen the length of the signature
    @param strict_param whether to enforce strictly canonical semantics

    @note For more details please see:
    https://ripple.com/wiki/Transaction_Malleability
    https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
    https://github.com/sipa/bitcoin/commit/58bc86e37fda1aec270bccb3df6c20fbd2a6591c
*/
bool isCanonicalECDSASig (void const* vSig, std::size_t sigLen, ECDSA strict_param)
{
    // The format of a signature should be:
    // <30> <len> [ <02> <lenR> <R> ] [ <02> <lenS> <S> ]

    unsigned char const* sig = reinterpret_cast<unsigned char const*> (vSig);

    if ((sigLen < 8) || (sigLen > 72))
        return false;

    if ((sig[0] != 0x30) || (sig[1] != (sigLen - 2)))
        return false;

    // The first two bytes are verified. Eat them.
    sig += 2;
    sigLen -= 2;

    // Verify the R signature
    detail::SignaturePart sigR (sig, sigLen);

    if (!sigR.valid ())
        return false;

    // Eat the number of bytes we consumed
    sig += sigR.skip ();
    sigLen -= sigR.skip ();

    // Verify the S signature
    detail::SignaturePart sigS (sig, sigLen);

    if (!sigS.valid ())
        return false;

    // Eat the number of bytes we consumed
    sig += sigS.skip ();
    sigLen -= sigS.skip ();

    // Nothing should remain at this point.
    if (sigLen != 0)
        return false;

    // Check whether R or S are greater than the modulus.
    auto bnR (sigR.getBigNum ());
    auto bnS (sigS.getBigNum ());

    if (BN_cmp (bnR, detail::modulus) != -1)
        return false;

    if (BN_cmp (bnS, detail::modulus) != -1)
        return false;

    // For a given signature, (R,S), the signature (R, N-S) is also valid. For
    // a signature to be fully-canonical, the smaller of these two values must
    // be specified. If operating in strict mode, check that as well.
    if (strict_param == ECDSA::strict)
    {
        detail::BigNum mS;

        if (BN_sub (mS, detail::modulus, bnS) == 0)
            return false;

        if (BN_cmp (bnS, mS) == 1)
            return false;
    }

    return true;
}

/** Convert a signature into strictly canonical form.
    Given the signature (R, S) then (R, G-S) is also valid. For a signature
    to be canonical, the smaller of { S, G-S } must be specified.
    @param vSig the signature we wish to convert
    @param sigLen the length of the signature
    @returns true if the signature was already canonical, false otherwise
*/
bool makeCanonicalECDSASig (void* vSig, std::size_t& sigLen)
{
    unsigned char * sig = reinterpret_cast<unsigned char *> (vSig);
    bool ret = false;

    // Find internals
    int rLen = sig[3];
    int sPos = rLen + 6, sLen = sig[rLen + 5];

    detail::BigNum origS, newS;
    BN_bin2bn (&sig[sPos], sLen, origS);
    BN_sub (newS, detail::modulus, origS);

    if (BN_cmp (origS, newS) == 1)
    { // original signature is not fully canonical
        unsigned char newSbuf [64];
        int newSlen = BN_bn2bin (newS, newSbuf);

        if ((newSbuf[0] & 0x80) == 0)
        { // no extra padding byte is needed
            sig[1] = sig[1] - sLen + newSlen;
            sig[sPos - 1] = newSlen;
            std::memcpy (&sig[sPos], newSbuf, newSlen);
        }
        else
        { // an extra padding byte is needed
            sig[1] = sig[1] - sLen + newSlen + 1;
            sig[sPos - 1] = newSlen + 1;
            sig[sPos] = 0;
            std::memcpy (&sig[sPos + 1], newSbuf, newSlen);
        }
        sigLen = sig[1] + 2;
    }
    else
        ret = true;

    return ret;
}

template <class FwdIter, class Container>
void hex_to_binary (FwdIter first, FwdIter last, Container& out)
{
    struct Table
    {
        int val[256];
        Table ()
        {
            std::fill (val, val+256, 0);
            for (int i = 0; i < 10; ++i)
                val ['0'+i] = i;
            for (int i = 0; i < 6; ++i)
            {
                val ['A'+i] = 10 + i;
                val ['a'+i] = 10 + i;
            }
        }
        int operator[] (int i)
        {
           return val[i];
        }
    };

    static Table lut;
    out.reserve (std::distance (first, last) / 2);
    while (first != last)
    {
        auto const hi (lut[(*first++)]);
        auto const lo (lut[(*first++)]);
        out.push_back ((hi*16)+lo);
    }
}

}
