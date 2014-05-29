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

#include <beast/unit_test/suite.h>
#include <algorithm>
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

        BigNum (unsigned char const* ptr, size_t len)
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

        bool set (unsigned char const* ptr, size_t len)
        {
            if (BN_bin2bn (ptr, len, num) == nullptr)
                return false;

            return true;
        }
    };

    class SignaturePart
    {
    private:
        size_t m_skip;
        BigNum m_bn;

    public:
        SignaturePart (unsigned char const* sig, size_t size)
            : m_skip (0)
        {
            // The format is: <02> <length of signature> <signature>
            if ((sig[0] != 0x02) || (size < 3))
                return;
            
            size_t const len (sig[1]);
            
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
        size_t skip () const
        {
            return m_skip;
        }
    };

    // The SECp256k1 modulus
    static BigNum const modulus (
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
}

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
bool isCanonicalECDSASig (void const* vSig, size_t sigLen, ECDSA strict_param)
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
bool makeCanonicalECDSASig (void* vSig, size_t& sigLen)
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

class ECDSACanonical_test : public beast::unit_test::suite
{
public:
    Blob loadSignature (std::string const& hex)
    {
        Blob b;
        hex_to_binary (hex.begin (), hex.end (), b);
        return b;
    }

    /** Verifies that a signature is syntactically valid.
    */
    bool isValid (std::string const& hex)
    {
        Blob j (loadSignature(hex));
        return isCanonicalECDSASig (&j[0], j.size(), ECDSA::not_strict);
    }

    /** Verifies that a signature is syntactically valid and in canonical form.
    */
    bool isStrictlyCanonical (std::string const& hex)
    {
        Blob j (loadSignature(hex));
        return isCanonicalECDSASig (&j[0], j.size (), ECDSA::strict);
    }

    /** Verify that we correctly identify strictly canonical signatures */
    void testStrictlyCanonicalSignatures ()
    {
        testcase ("Strictly canonical signature checks", abort_on_fail);

        expect (isStrictlyCanonical("3045"
            "022100FF478110D1D4294471EC76E0157540C2181F47DEBD25D7F9E7DDCCCD47EEE905"
            "0220078F07CDAE6C240855D084AD91D1479609533C147C93B0AEF19BC9724D003F28"),
            "Strictly canonical signature");
 
        expect (isStrictlyCanonical("3045"
            "0221009218248292F1762D8A51BE80F8A7F2CD288D810CE781D5955700DA1684DF1D2D"
            "022041A1EE1746BFD72C9760CC93A7AAA8047D52C8833A03A20EAAE92EA19717B454"),
            "Strictly canonical signature");
 
        expect (isStrictlyCanonical("3044"
            "02206A9E43775F73B6D1EC420E4DDD222A80D4C6DF5D1BEECC431A91B63C928B7581"
            "022023E9CC2D61DDA6F73EAA6BCB12688BEB0F434769276B3127E4044ED895C9D96B"),
            "Strictly canonical signature");

         expect (isStrictlyCanonical("3044"
            "022056E720007221F3CD4EFBB6352741D8E5A0968D48D8D032C2FBC4F6304AD1D04E"
            "02201F39EB392C20D7801C3E8D81D487E742FA84A1665E923225BD6323847C71879F"),
            "Strictly canonical signature");

        expect (isStrictlyCanonical("3045"
            "022100FDFD5AD05518CEA0017A2DCB5C4DF61E7C73B6D3A38E7AE93210A1564E8C2F12"
            "0220214FF061CCC123C81D0BB9D0EDEA04CD40D96BF1425D311DA62A7096BB18EA18"),
            "Strictly canonical signature");

        // These are canonical signatures, but *not* strictly canonical.
        expect (!isStrictlyCanonical ("3046"
            "022100F477B3FA6F31C7CB3A0D1AD94A231FDD24B8D78862EE334CEA7CD08F6CBC0A1B"
            "022100928E6BCF1ED2684679730C5414AEC48FD62282B090041C41453C1D064AF597A1"),
            "Not strictly canonical signature");

        expect (!isStrictlyCanonical ("3045"
            "022063E7C7CA93CB2400E413A342C027D00665F8BAB9C22EF0A7B8AE3AAF092230B6"
            "0221008F2E8BB7D09521ABBC277717B14B93170AE6465C5A1B36561099319C4BEB254C"),
            "Not strictly canonical signature");

        expect (!isStrictlyCanonical ("3046"
            "02210099DCA1188663DDEA506A06A7B20C2B7D8C26AFF41DECE69D6C5F7C967D32625F"
            "022100897658A6B1F9EEE5D140D7A332DA0BD73BB98974EA53F6201B01C1B594F286EA"),
            "Not strictly canonical signature");

        expect (!isStrictlyCanonical ("3045"
            "02200855DE366E4E323AA2CE2A25674401A7D11F72EC432770D07F7B57DF7387AEC0"
            "022100DA4C6ADDEA14888858DE2AC5B91ED9050D6972BB388DEF582628CEE32869AE35"),
            "Not strictly canonical signature");
    }

    /** Verify that we correctly identify valid signatures */
    void testValidSignatures ()
    {
        testcase ("Canonical signature checks", abort_on_fail);

        // r and s 1 byte 1
        expect (isValid ("3006" 
            "020101" 
            "020102"),
            "Well-formed short signature");

        expect (isValid ("3044"
            "02203932c892e2e550f3af8ee4ce9c215a87f9bb831dcac87b2838e2c2eaa891df0c"
            "022030b61dd36543125d56b9f9f3a1f53189e5af33cdda8d77a5209aec03978fa001"),
            "Canonical signature");

        expect (isValid ("3045"
            "0220076045be6f9eca28ff1ec606b833d0b87e70b2a630f5e3a496b110967a40f90a"
            "0221008fffd599910eefe00bc803c688eca1d2ba7f6b180620eaa03488e6585db6ba01"),
            "Canonical signature");

        expect (isValid("3046"
            "022100876045be6f9eca28ff1ec606b833d0b87e70b2a630f5e3a496b110967a40f90a"
            "0221008fffd599910eefe00bc803c688c2eca1d2ba7f6b180620eaa03488e6585db6ba"),
            "Canonical signature");

        expect (isStrictlyCanonical("3045"
            "022100FF478110D1D4294471EC76E0157540C2181F47DEBD25D7F9E7DDCCCD47EEE905"
            "0220078F07CDAE6C240855D084AD91D1479609533C147C93B0AEF19BC9724D003F28"),
            "Canonical signature");
 
        expect (isStrictlyCanonical("3045"
            "0221009218248292F1762D8A51BE80F8A7F2CD288D810CE781D5955700DA1684DF1D2D"
            "022041A1EE1746BFD72C9760CC93A7AAA8047D52C8833A03A20EAAE92EA19717B454"),
            "Canonical signature");
 
        expect (isStrictlyCanonical("3044"
            "02206A9E43775F73B6D1EC420E4DDD222A80D4C6DF5D1BEECC431A91B63C928B7581"
            "022023E9CC2D61DDA6F73EAA6BCB12688BEB0F434769276B3127E4044ED895C9D96B"),
            "Canonical signature");

         expect (isStrictlyCanonical("3044"
            "022056E720007221F3CD4EFBB6352741D8E5A0968D48D8D032C2FBC4F6304AD1D04E"
            "02201F39EB392C20D7801C3E8D81D487E742FA84A1665E923225BD6323847C71879F"),
            "Canonical signature");

        expect (isStrictlyCanonical("3045"
            "022100FDFD5AD05518CEA0017A2DCB5C4DF61E7C73B6D3A38E7AE93210A1564E8C2F12"
            "0220214FF061CCC123C81D0BB9D0EDEA04CD40D96BF1425D311DA62A7096BB18EA18"),
            "Canonical signature");
    }

    /** Verify that we correctly identify malformed or invalid signatures */
    void testMalformedSignatures ()
    {
        testcase ("Non-canonical signature checks", abort_on_fail);

        expect (!isValid("3005"
            "0201FF"
            "0200"),
            "tooshort");

        expect (!isValid ("3006" 
            "020101" 
            "020202"),
            "Slen-Overlong");

        expect (!isValid ("3006" 
            "020701" 
            "020102"),
            "Rlen-Overlong-OOB");

        expect (!isValid ("3006" 
            "020401" 
            "020102"),
            "Rlen-Overlong-OOB");

        expect (!isValid ("3006" 
            "020501" 
            "020102"),
            "Rlen-Overlong-OOB");

        expect (!isValid ("3006" 
            "020201" 
            "020102"),
            "Rlen-Overlong");

        expect (!isValid ("3006" 
            "020301" 
            "020202"),
            "Rlen Overlong and Slen-Overlong");

        expect (!isValid ("3006" 
            "020401" 
            "020202"),
            "Rlen Overlong and OOB and Slen-Overlong");

        expect (!isValid("3047"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "022200002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "toolong");

        expect (!isValid("3144"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "type");

        expect (!isValid("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "totallength");

        expect (!isValid("301F"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1"),
            "Slenoob");

        expect (!isValid("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed00"),
            "R+S");

        expect (!isValid("3044"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rtype");

        expect (!isValid("3024"
            "0200"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rlen=0");

        expect (!isValid("3044"
            "02208990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "R<0");

        expect (!isValid("3045"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rpadded");

        expect (!isValid("3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105012"
            "02d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Stype");

        expect (!isValid("3024"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0200"),
            "Slen=0");

        expect (!isValid("3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0220fd5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "S<0");

        expect (!isValid("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0221002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Spadded");
    }

    void convertNonCanonical(std::string const& hex, std::string const& canonHex)
    {
        Blob b (loadSignature(hex));

        // The signature ought to at least be valid before we begin.
        expect (isValid (hex), "invalid signature");

        size_t len = b.size ();

        expect (!makeCanonicalECDSASig (&b[0], len), 
            "non-canonical signature was already canonical");

        expect (b.size () >= len,
            "canonicalized signature length longer than non-canonical");

        b.resize (len);

        expect (isCanonicalECDSASig (&b[0], len, ECDSA::strict),
            "canonicalized signature is not strictly canonical");

        Blob canonicalForm (loadSignature (canonHex));

        expect (b.size () == canonicalForm.size (),
            "canonicalized signature doesn't have the expected length");

        expect (std::equal (b.begin (), b.end (), canonicalForm.begin ()),
            "canonicalized signature isn't what we expected");
    }

    /** Verifies correctness of non-canonical to canonical conversion */
    void testCanonicalConversions()
    {
        testcase ("Non-canonical signature canonicalization", abort_on_fail);

        convertNonCanonical (
            "3046"
                "022100F477B3FA6F31C7CB3A0D1AD94A231FDD24B8D78862EE334CEA7CD08F6CBC0A1B"
                "022100928E6BCF1ED2684679730C5414AEC48FD62282B090041C41453C1D064AF597A1",
            "3045"
                "022100F477B3FA6F31C7CB3A0D1AD94A231FDD24B8D78862EE334CEA7CD08F6CBC0A1B"
                "02206D719430E12D97B9868CF3ABEB513B6EE48C5A361F4483FA7A9641868540A9A0");

        convertNonCanonical (
            "3045"
                "022063E7C7CA93CB2400E413A342C027D00665F8BAB9C22EF0A7B8AE3AAF092230B6"
                "0221008F2E8BB7D09521ABBC277717B14B93170AE6465C5A1B36561099319C4BEB254C",
            "3044"
                "022063E7C7CA93CB2400E413A342C027D00665F8BAB9C22EF0A7B8AE3AAF092230B6"
                "022070D174482F6ADE5443D888E84EB46CE7AFC8968A552D69E5AF392CF0844B1BF5");

        convertNonCanonical (
            "3046"
                "02210099DCA1188663DDEA506A06A7B20C2B7D8C26AFF41DECE69D6C5F7C967D32625F"
                "022100897658A6B1F9EEE5D140D7A332DA0BD73BB98974EA53F6201B01C1B594F286EA",
            "3045"
                "02210099DCA1188663DDEA506A06A7B20C2B7D8C26AFF41DECE69D6C5F7C967D32625F"
                "02207689A7594E06111A2EBF285CCD25F4277EF55371C4F4AA1BA4D09CD73B43BA57");
 
        convertNonCanonical (
            "3045"
                "02200855DE366E4E323AA2CE2A25674401A7D11F72EC432770D07F7B57DF7387AEC0"
                "022100DA4C6ADDEA14888858DE2AC5B91ED9050D6972BB388DEF582628CEE32869AE35",
            "3044"
                "02200855DE366E4E323AA2CE2A25674401A7D11F72EC432770D07F7B57DF7387AEC0"
                "022025B3952215EB7777A721D53A46E126F9AD456A2B76BAB0E399A98FA9A7CC930C");
    }

    void run ()
    {
        testValidSignatures ();

        testStrictlyCanonicalSignatures ();

        testMalformedSignatures ();

        testCanonicalConversions ();
    }
};

BEAST_DEFINE_TESTSUITE(ECDSACanonical,sslutil,ripple);

}
