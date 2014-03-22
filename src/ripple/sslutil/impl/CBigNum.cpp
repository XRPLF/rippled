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

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

namespace ripple {

CBigNum::CBigNum ()
{
    BN_init (this);
}

CBigNum::CBigNum (const CBigNum& b)
{
    BN_init (this);

    if (!BN_copy (this, &b))
    {
        BN_clear_free (this);
        throw bignum_error ("CBigNum::CBigNum(const CBigNum&) : BN_copy failed");
    }
}

CBigNum& CBigNum::operator= (const CBigNum& b)
{
    if (!BN_copy (this, &b))
        throw bignum_error ("CBigNum::operator= : BN_copy failed");

    return (*this);
}

CBigNum::~CBigNum ()
{
    BN_clear_free (this);
}

CBigNum::CBigNum (char n)
{
    BN_init (this);

    if (n >= 0) setulong (n);
    else setint64 (n);
}
CBigNum::CBigNum (short n)
{
    BN_init (this);

    if (n >= 0) setulong (n);
    else setint64 (n);
}
CBigNum::CBigNum (int n)
{
    BN_init (this);

    if (n >= 0) setulong (n);
    else setint64 (n);
}
CBigNum::CBigNum (long n)
{
    BN_init (this);

    if (n >= 0) setulong (n);
    else setint64 (n);
}
CBigNum::CBigNum (long long n)
{
    BN_init (this);
    setint64 (n);
}
CBigNum::CBigNum (unsigned char n)
{
    BN_init (this);
    setulong (n);
}
CBigNum::CBigNum (unsigned short n)
{
    BN_init (this);
    setulong (n);
}
CBigNum::CBigNum (unsigned int n)
{
    BN_init (this);
    setulong (n);
}
CBigNum::CBigNum (unsigned long long n)
{
    BN_init (this);
    setuint64 (n);
}
CBigNum::CBigNum (uint256 n)
{
    BN_init (this);
    setuint256 (n);
}

CBigNum::CBigNum (Blob const& vch)
{
    BN_init (this);
    setvch (&vch.front(), &vch.back()+1);
}

CBigNum::CBigNum (unsigned char const* begin, unsigned char const* end)
{
    BN_init (this);
    setvch (begin, end);
}

void CBigNum::setuint (unsigned int n)
{
    setulong (static_cast<unsigned long> (n));
}

unsigned int CBigNum::getuint () const
{
    return BN_get_word (this);
}

int CBigNum::getint () const
{
    unsigned long n = BN_get_word (this);

    if (!BN_is_negative (this))
        return (n > INT_MAX ? INT_MAX : n);
    else
        return (n > INT_MAX ? INT_MIN : - (int)n);
}

void CBigNum::setint64 (std::int64_t n)
{
    unsigned char pch[sizeof (n) + 6];
    unsigned char* p = pch + 4;
    bool fNegative = false;

    if (n < (std::int64_t)0)
    {
        n = -n;
        fNegative = true;
    }

    bool fLeadingZeroes = true;

    for (int i = 0; i < 8; i++)
    {
        unsigned char c = (n >> 56) & 0xff;
        n <<= 8;

        if (fLeadingZeroes)
        {
            if (c == 0)
                continue;

            if (c & 0x80)
                *p++ = (fNegative ? 0x80 : 0);
            else if (fNegative)
                c |= 0x80;

            fLeadingZeroes = false;
        }

        *p++ = c;
    }

    unsigned int nSize = p - (pch + 4);
    pch[0] = (nSize >> 24) & 0xff;
    pch[1] = (nSize >> 16) & 0xff;
    pch[2] = (nSize >> 8) & 0xff;
    pch[3] = (nSize) & 0xff;
    BN_mpi2bn (pch, p - pch, this);
}

std::uint64_t CBigNum::getuint64 () const
{
#if (ULONG_MAX > UINT_MAX)
    return static_cast<std::uint64_t> (getulong ());
#else
    int len = BN_num_bytes (this);

    if (len > 8)
        throw std::runtime_error ("BN getuint64 overflow");

    unsigned char buf[8];
    memset (buf, 0, sizeof (buf));
    BN_bn2bin (this, buf + 8 - len);
    return
        static_cast<std::uint64_t> (buf[0]) << 56 | static_cast<std::uint64_t> (buf[1]) << 48 |
        static_cast<std::uint64_t> (buf[2]) << 40 | static_cast<std::uint64_t> (buf[3]) << 32 |
        static_cast<std::uint64_t> (buf[4]) << 24 | static_cast<std::uint64_t> (buf[5]) << 16 |
        static_cast<std::uint64_t> (buf[6]) << 8 | static_cast<std::uint64_t> (buf[7]);
#endif
}

void CBigNum::setuint64 (std::uint64_t n)
{
#if (ULONG_MAX > UINT_MAX)
    setulong (static_cast<unsigned long> (n));
#else
    unsigned char buf[8];
    buf[0] = static_cast<unsigned char> ((n >> 56) & 0xff);
    buf[1] = static_cast<unsigned char> ((n >> 48) & 0xff);
    buf[2] = static_cast<unsigned char> ((n >> 40) & 0xff);
    buf[3] = static_cast<unsigned char> ((n >> 32) & 0xff);
    buf[4] = static_cast<unsigned char> ((n >> 24) & 0xff);
    buf[5] = static_cast<unsigned char> ((n >> 16) & 0xff);
    buf[6] = static_cast<unsigned char> ((n >> 8) & 0xff);
    buf[7] = static_cast<unsigned char> ((n) & 0xff);
    BN_bin2bn (buf, 8, this);
#endif
}

void CBigNum::setuint256 (uint256 const& n)
{
    BN_bin2bn (n.begin (), n.size (), this);
}

uint256 CBigNum::getuint256 ()
{
    uint256 ret;
    unsigned int size = BN_num_bytes (this);

    if (size > ret.size ())
        return ret;

    BN_bn2bin (this, ret.begin () + (ret.size () - BN_num_bytes (this)));
    return ret;
}

void CBigNum::setvch (unsigned char const* begin, unsigned char const* end)
{
    std::size_t const size (std::distance (begin, end));
    Blob vch2 (size + 4);
    unsigned int nSize (size);
    // BIGNUM's byte stream format expects 4 bytes of
    // big endian size data info at the front
    vch2[0] = (nSize >> 24) & 0xff;
    vch2[1] = (nSize >> 16) & 0xff;
    vch2[2] = (nSize >> 8) & 0xff;
    vch2[3] = (nSize >> 0) & 0xff;
    // swap data to big endian
    std::reverse_copy (begin, end, vch2.begin() + 4);
    BN_mpi2bn (&vch2[0], vch2.size (), this);
}

void CBigNum::setvch (Blob const& vch)
{
    setvch (&vch.front(), &vch.back()+1);
}

Blob CBigNum::getvch () const
{
    unsigned int nSize = BN_bn2mpi (this, nullptr);

    if (nSize < 4)
        return Blob ();

    Blob vch (nSize);
    BN_bn2mpi (this, &vch[0]);
    vch.erase (vch.begin (), vch.begin () + 4);
    reverse (vch.begin (), vch.end ());
    return vch;
}

CBigNum& CBigNum::SetCompact (unsigned int nCompact)
{
    unsigned int nSize = nCompact >> 24;
    Blob vch (4 + nSize);
    vch[3] = nSize;

    if (nSize >= 1) vch[4] = (nCompact >> 16) & 0xff;

    if (nSize >= 2) vch[5] = (nCompact >> 8) & 0xff;

    if (nSize >= 3) vch[6] = (nCompact >> 0) & 0xff;

    BN_mpi2bn (&vch[0], vch.size (), this);
    return *this;
}

unsigned int CBigNum::GetCompact () const
{
    unsigned int nSize = BN_bn2mpi (this, nullptr);
    Blob vch (nSize);
    nSize -= 4;
    BN_bn2mpi (this, &vch[0]);
    unsigned int nCompact = nSize << 24;

    if (nSize >= 1) nCompact |= (vch[4] << 16);

    if (nSize >= 2) nCompact |= (vch[5] << 8);

    if (nSize >= 3) nCompact |= (vch[6] << 0);

    return nCompact;
}

void CBigNum::SetHex (const std::string& str)
{
    // skip 0x
    const char* psz = str.c_str ();

    while (isspace (*psz))
        psz++;

    bool fNegative = false;

    if (*psz == '-')
    {
        fNegative = true;
        psz++;
    }

    if (psz[0] == '0' && tolower (psz[1]) == 'x')
        psz += 2;

    while (isspace (*psz))
        psz++;

    // hex string to bignum
    static char phexdigit[256] =
    {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
        0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    *this = 0;

    while (isxdigit (*psz))
    {
        *this <<= 4;
        int n = phexdigit[ (int) * psz++];
        *this += n;
    }

    if (fNegative)
        *this = 0 - *this;
}

std::string CBigNum::ToString (int nBase) const
{
    CAutoBN_CTX pctx;
    CBigNum bnBase = nBase;
    CBigNum bn0 = 0;
    std::string str;
    CBigNum bn = *this;
    BN_set_negative (&bn, false);
    CBigNum dv;
    CBigNum rem;

    if (BN_cmp (&bn, &bn0) == 0)
        return "0";

    while (BN_cmp (&bn, &bn0) > 0)
    {
        if (!BN_div (&dv, &rem, &bn, &bnBase, pctx))
            throw bignum_error ("CBigNum::ToString() : BN_div failed");

        bn = dv;
        unsigned int c = rem.getuint ();
        str += "0123456789abcdef"[c];
    }

    if (BN_is_negative (this))
        str += "-";

    reverse (str.begin (), str.end ());
    return str;
}

std::string CBigNum::GetHex () const
{
    return ToString (16);
}

bool CBigNum::operator! () const
{
    return BN_is_zero (this);
}

CBigNum& CBigNum::operator+= (const CBigNum& b)
{
    if (!BN_add (this, this, &b))
        throw bignum_error ("CBigNum::operator+= : BN_add failed");

    return *this;
}

CBigNum& CBigNum::operator-= (const CBigNum& b)
{
    *this = *this - b;
    return *this;
}

CBigNum& CBigNum::operator*= (const CBigNum& b)
{
    CAutoBN_CTX pctx;

    if (!BN_mul (this, this, &b, pctx))
        throw bignum_error ("CBigNum::operator*= : BN_mul failed");

    return *this;
}

CBigNum& CBigNum::operator/= (const CBigNum& b)
{
    *this = *this / b;
    return *this;
}

CBigNum& CBigNum::operator%= (const CBigNum& b)
{
    *this = *this % b;
    return *this;
}

CBigNum& CBigNum::operator<<= (unsigned int shift)
{
    if (!BN_lshift (this, this, shift))
        throw bignum_error ("CBigNum:operator<<= : BN_lshift failed");

    return *this;
}

CBigNum& CBigNum::operator>>= (unsigned int shift)
{
    // Note: BN_rshift segfaults on 64-bit if 2^shift is greater than the number
    //   if built on ubuntu 9.04 or 9.10, probably depends on version of openssl
    CBigNum a = 1;
    a <<= shift;

    if (BN_cmp (&a, this) > 0)
    {
        *this = 0;
        return *this;
    }

    if (!BN_rshift (this, this, shift))
        throw bignum_error ("CBigNum:operator>>= : BN_rshift failed");

    return *this;
}


CBigNum& CBigNum::operator++ ()
{
    // prefix operator
    if (!BN_add (this, this, BN_value_one ()))
        throw bignum_error ("CBigNum::operator++ : BN_add failed");

    return *this;
}

const CBigNum CBigNum::operator++ (int)
{
    // postfix operator
    const CBigNum ret = *this;
    ++ (*this);
    return ret;
}

CBigNum& CBigNum::operator-- ()
{
    // prefix operator
    CBigNum r;

    if (!BN_sub (&r, this, BN_value_one ()))
        throw bignum_error ("CBigNum::operator-- : BN_sub failed");

    *this = r;
    return *this;
}

const CBigNum CBigNum::operator-- (int)
{
    // postfix operator
    const CBigNum ret = *this;
    -- (*this);
    return ret;
}

void CBigNum::setulong (unsigned long n)
{
    if (!BN_set_word (this, n))
        throw bignum_error ("CBigNum conversion from unsigned long : BN_set_word failed");
}

unsigned long CBigNum::getulong () const
{
    return BN_get_word (this);
}

const CBigNum operator+ (const CBigNum& a, const CBigNum& b)
{
    CBigNum r;

    if (!BN_add (&r, &a, &b))
        throw bignum_error ("CBigNum::operator+ : BN_add failed");

    return r;
}

const CBigNum operator- (const CBigNum& a, const CBigNum& b)
{
    CBigNum r;

    if (!BN_sub (&r, &a, &b))
        throw bignum_error ("CBigNum::operator- : BN_sub failed");

    return r;
}

const CBigNum operator- (const CBigNum& a)
{
    CBigNum r (a);
    BN_set_negative (&r, !BN_is_negative (&r));
    return r;
}

const CBigNum operator* (const CBigNum& a, const CBigNum& b)
{
    CAutoBN_CTX pctx;
    CBigNum r;

    if (!BN_mul (&r, &a, &b, pctx))
        throw bignum_error ("CBigNum::operator* : BN_mul failed");

    return r;
}

const CBigNum operator/ (const CBigNum& a, const CBigNum& b)
{
    CAutoBN_CTX pctx;
    CBigNum r;

    if (!BN_div (&r, nullptr, &a, &b, pctx))
        throw bignum_error ("CBigNum::operator/ : BN_div failed");

    return r;
}

const CBigNum operator% (const CBigNum& a, const CBigNum& b)
{
    CAutoBN_CTX pctx;
    CBigNum r;

    if (!BN_mod (&r, &a, &b, pctx))
        throw bignum_error ("CBigNum::operator% : BN_div failed");

    return r;
}

const CBigNum operator<< (const CBigNum& a, unsigned int shift)
{
    CBigNum r;

    if (!BN_lshift (&r, &a, shift))
        throw bignum_error ("CBigNum:operator<< : BN_lshift failed");

    return r;
}

const CBigNum operator>> (const CBigNum& a, unsigned int shift)
{
    CBigNum r = a;
    r >>= shift;
    return r;
}

bool operator== (const CBigNum& a, const CBigNum& b)
{
    return (BN_cmp (&a, &b) == 0);
}

bool operator!= (const CBigNum& a, const CBigNum& b)
{
    return (BN_cmp (&a, &b) != 0);
}

bool operator<= (const CBigNum& a, const CBigNum& b)
{
    return (BN_cmp (&a, &b) <= 0);
}

bool operator>= (const CBigNum& a, const CBigNum& b)
{
    return (BN_cmp (&a, &b) >= 0);
}

bool operator<  (const CBigNum& a, const CBigNum& b)
{
    return (BN_cmp (&a, &b) < 0);
}

bool operator>  (const CBigNum& a, const CBigNum& b)
{
    return (BN_cmp (&a, &b) > 0);
}

#if (ULONG_MAX > UINT_MAX)

int BN_add_word64 (BIGNUM* bn, std::uint64_t word)
{
    return BN_add_word (bn, word);
}

int BN_sub_word64 (BIGNUM* bn, std::uint64_t word)
{
    return BN_sub_word (bn, word);
}

int BN_mul_word64 (BIGNUM* bn, std::uint64_t word)
{
    return BN_mul_word (bn, word);
}

std::uint64_t BN_div_word64 (BIGNUM* bn, std::uint64_t word)
{
    return BN_div_word (bn, word);
}

#else

int BN_add_word64 (BIGNUM* a, std::uint64_t w)
{
    CBigNum bn (w);
    return BN_add (a, &bn, a);
}

int BN_sub_word64 (BIGNUM* a, std::uint64_t w)
{
    CBigNum bn (w);
    return BN_sub (a, &bn, a);
}

int BN_mul_word64 (BIGNUM* a, std::uint64_t w)
{
    CBigNum bn (w);
    CAutoBN_CTX ctx;
    return BN_mul (a, &bn, a, ctx);
}

std::uint64_t BN_div_word64 (BIGNUM* a, std::uint64_t w)
{
    CBigNum bn (w);
    CAutoBN_CTX ctx;
    return (BN_div (a, nullptr, a, &bn, ctx) == 1) ? 0 : ((std::uint64_t) - 1);
}

#endif

}
