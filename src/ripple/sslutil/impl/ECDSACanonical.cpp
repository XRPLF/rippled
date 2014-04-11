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

namespace ripple {

namespace detail {
    // A simple wrapper for a BIGNUM to make it
    // easier to allocate, construct, and free them
    struct BigNum
    {
        BIGNUM* num;

        BigNum (BigNum const&) = delete;
        BigNum& operator=(BigNum const&) = delete;

        BigNum (const char *hex)
        {
            num = BN_new ();
            BN_hex2bn (&num, hex);
        }

        BigNum ()
        {
            num = BN_new ();
        }

        BigNum (unsigned char const* ptr, size_t len)
        {
            num = BN_new ();
            BN_bin2bn (ptr, len, num);
        }

        ~BigNum ()
        {
            BN_free (num);
        }

        operator BIGNUM* ()
        {
            return num;
        }
    };

    // The SECp256k1 modulus
    static BigNum modulus ("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
}

bool isCanonicalECDSASig (void const* vSig, size_t sigLen, ECDSA strict_param)
{
    // Make sure signature is canonical
    // To protect against signature morphing attacks
    // See https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
    // and https://github.com/sipa/bitcoin/commit/58bc86e37fda1aec270bccb3df6c20fbd2a6591c

    // Signature should be:
    // <30> <len> [ <02> <lenR> <R> ] [ <02> <lenS> <S> ]

    const bool strict = strict_param == ECDSA::strict;

    unsigned char const* sig = reinterpret_cast<unsigned char const*> (vSig);

    if ((sigLen < 10) || (sigLen > 74))
        return false;

    if ((sig[0] != 0x30) || (sig[1] != (sigLen - 2)))
        return false;

    // Find R and check its length
    int rPos = 4, rLen = sig[3];
    if ((rLen < 2) || ((rLen + 6) > sigLen))
        return false;

    // Find S and check its length
    int sPos = rLen + 6, sLen = sig [rLen + 5];
    if ((sLen < 2) || ((rLen + sLen + 6) != sigLen))
        return false;

    if ((sig[rPos - 2] != 0x02) || (sig[sPos - 2] != 0x02))
        return false; // R or S have wrong type

    if ((sig[rPos] & 0x80) != 0)
        return false; // R is negative

    if ((sig[rPos] == 0) && ((sig[rPos + 1] & 0x80) == 0))
        return false; // R is padded

    if ((sig[sPos] & 0x80) != 0)
        return false; // S is negative

    if ((sig[sPos] == 0) && ((sig[sPos + 1] & 0x80) == 0))
        return false; // S is padded

    detail::BigNum bnR (&sig[rPos], rLen);
    detail::BigNum bnS (&sig[sPos], sLen);
    if ((BN_cmp (bnR, detail::modulus) != -1) || (BN_cmp (bnS, detail::modulus) != -1))
        return false; // R or S greater than modulus

    if (!strict)
        return true;

    detail::BigNum mS;
    BN_sub (mS, detail::modulus, bnS);
    return BN_cmp (bnS, mS) != 1;
}


// Returns true if original signature was alread canonical
bool makeCanonicalECDSASig (void* vSig, size_t& sigLen)
{
// Signature is (r,s) where 0 < s < g
// If (g-s)<g, replace signature with (r,g-s)

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
            memcpy (&sig[sPos], newSbuf, newSlen);
        }
        else
        { // an extra padding byte is needed
            sig[1] = sig[1] - sLen + newSlen + 1;
            sig[sPos - 1] = newSlen + 1;
            sig[sPos] = 0;
            memcpy (&sig[sPos + 1], newSbuf, newSlen);
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

class ECDSACanonicalTests : public UnitTest
{
public:
    ECDSACanonicalTests () : UnitTest ("ECDSACanonical", "ripple")
    {
    }

    bool isCanonical (std::string const& hex)
    {
        Blob j;
        hex_to_binary (hex.begin(), hex.end(), j);
        return isCanonicalECDSASig (&j[0], j.size(), ECDSA::not_strict);
    }

    void runTest ()
    {
        beginTestCase ("canonicalSignatures");

        expect (isCanonical("304402203932c892e2e550f3af8ee4ce9c215a87f9bb"
            "831dcac87b2838e2c2eaa891df0c022030b61dd36543125d56b9f9f3a1f"
            "53189e5af33cdda8d77a5209aec03978fa001"), "canonical signature");

        expect (isCanonical("30450220076045be6f9eca28ff1ec606b833d0b87e70b"
            "2a630f5e3a496b110967a40f90a0221008fffd599910eefe00bc803c688"
            "eca1d2ba7f6b180620eaa03488e6585db6ba01"), "canonical signature");

        expect (isCanonical("3046022100876045be6f9eca28ff1ec606b833d0b87e7"
            "0b2a630f5e3a496b110967a40f90a0221008fffd599910eefe00bc803c688c"
            "2eca1d2ba7f6b180620eaa03488e6585db6ba"), "canonical signature");

        expect (!isCanonical("3005" "0201FF" "0200"), "tooshort");

        expect (!isCanonical("3047"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "022200002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "toolong");

        expect (!isCanonical("3144"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "type");

        expect (!isCanonical("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "totallength");

        expect (!isCanonical(
            "301F" "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1"),
            "Slenoob");

        expect (!isCanonical("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed00"),
            "R+S");

        expect (!isCanonical("3044"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rtype");

        expect (!isCanonical("3024" "0200"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rlen=0");

        expect (!isCanonical("3044"
            "02208990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "R<0");

        expect (!isCanonical("3045"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rpadded");

        expect (!isCanonical("3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105012"
            "02d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Stype");

        expect (!isCanonical("3024"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0200"),
            "Slen=0");

        expect (!isCanonical("3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0220fd5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "S<0");

        expect (!isCanonical("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0221002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Spadded");

    }
};

static ECDSACanonicalTests ECDSACTests;

}
