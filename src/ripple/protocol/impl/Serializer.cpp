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
#include <ripple/basics/Log.h>
#include <ripple/protocol/Serializer.h>
#include <openssl/ripemd.h>
#include <openssl/pem.h>

namespace ripple {

int Serializer::addZeros (size_t uBytes)
{
    int ret = mData.size ();

    while (uBytes--)
        mData.push_back (0);

    return ret;
}

int Serializer::add16 (std::uint16_t i)
{
    int ret = mData.size ();
    mData.push_back (static_cast<unsigned char> (i >> 8));
    mData.push_back (static_cast<unsigned char> (i & 0xff));
    return ret;
}

int Serializer::add32 (std::uint32_t i)
{
    int ret = mData.size ();
    mData.push_back (static_cast<unsigned char> (i >> 24));
    mData.push_back (static_cast<unsigned char> ((i >> 16) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 8) & 0xff));
    mData.push_back (static_cast<unsigned char> (i & 0xff));
    return ret;
}

int Serializer::add64 (std::uint64_t i)
{
    int ret = mData.size ();
    mData.push_back (static_cast<unsigned char> (i >> 56));
    mData.push_back (static_cast<unsigned char> ((i >> 48) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 40) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 32) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 24) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 16) & 0xff));
    mData.push_back (static_cast<unsigned char> ((i >> 8) & 0xff));
    mData.push_back (static_cast<unsigned char> (i & 0xff));
    return ret;
}

template <> int Serializer::addInteger(unsigned char i) { return add8(i); }
template <> int Serializer::addInteger(std::uint16_t i) { return add16(i); }
template <> int Serializer::addInteger(std::uint32_t i) { return add32(i); }
template <> int Serializer::addInteger(std::uint64_t i) { return add64(i); }

int Serializer::add128 (const uint128& i)
{
    int ret = mData.size ();
    mData.insert (mData.end (), i.begin (), i.end ());
    return ret;
}

int Serializer::add256 (uint256 const& i)
{
    int ret = mData.size ();
    mData.insert (mData.end (), i.begin (), i.end ());
    return ret;
}

int Serializer::addRaw (Blob const& vector)
{
    int ret = mData.size ();
    mData.insert (mData.end (), vector.begin (), vector.end ());
    return ret;
}

int Serializer::addRaw (const Serializer& s)
{
    int ret = mData.size ();
    mData.insert (mData.end (), s.begin (), s.end ());
    return ret;
}

int Serializer::addRaw (const void* ptr, int len)
{
    int ret = mData.size ();
    mData.insert (mData.end (), (const char*) ptr, ((const char*)ptr) + len);
    return ret;
}

bool Serializer::get16 (std::uint16_t& o, int offset) const
{
    if ((offset + 2) > mData.size ()) return false;

    const unsigned char* ptr = &mData[offset];
    o = *ptr++;
    o <<= 8;
    o |= *ptr;
    return true;
}

bool Serializer::get32 (std::uint32_t& o, int offset) const
{
    if ((offset + 4) > mData.size ()) return false;

    const unsigned char* ptr = &mData[offset];
    o = *ptr++;
    o <<= 8;
    o |= *ptr++;
    o <<= 8;
    o |= *ptr++;
    o <<= 8;
    o |= *ptr;
    return true;
}

bool Serializer::get64 (std::uint64_t& o, int offset) const
{
    if ((offset + 8) > mData.size ()) return false;

    const unsigned char* ptr = &mData[offset];
    o = *ptr++;
    o <<= 8;
    o |= *ptr++;
    o <<= 8;
    o |= *ptr++;
    o <<= 8;
    o |= *ptr++;
    o <<= 8;
    o |= *ptr++;
    o <<= 8;
    o |= *ptr++;
    o <<= 8;
    o |= *ptr++;
    o <<= 8;
    o |= *ptr;
    return true;
}

bool Serializer::get128 (uint128& o, int offset) const
{
    if ((offset + (128 / 8)) > mData.size ()) return false;

    memcpy (o.begin (), & (mData.front ()) + offset, (128 / 8));
    return true;
}

bool Serializer::get256 (uint256& o, int offset) const
{
    if ((offset + (256 / 8)) > mData.size ()) return false;

    memcpy (o.begin (), & (mData.front ()) + offset, (256 / 8));
    return true;
}

uint256 Serializer::get256 (int offset) const
{
    uint256 ret;

    if ((offset + (256 / 8)) > mData.size ()) return ret;

    memcpy (ret.begin (), & (mData.front ()) + offset, (256 / 8));
    return ret;
}

int Serializer::addFieldID (int type, int name)
{
    int ret = mData.size ();
    assert ((type > 0) && (type < 256) && (name > 0) && (name < 256));

    if (type < 16)
    {
        if (name < 16) // common type, common name
            mData.push_back (static_cast<unsigned char> ((type << 4) | name));
        else
        {
            // common type, uncommon name
            mData.push_back (static_cast<unsigned char> (type << 4));
            mData.push_back (static_cast<unsigned char> (name));
        }
    }
    else if (name < 16)
    {
        // uncommon type, common name
        mData.push_back (static_cast<unsigned char> (name));
        mData.push_back (static_cast<unsigned char> (type));
    }
    else
    {
        // uncommon type, uncommon name
        mData.push_back (static_cast<unsigned char> (0));
        mData.push_back (static_cast<unsigned char> (type));
        mData.push_back (static_cast<unsigned char> (name));
    }

    return ret;
}

bool Serializer::getFieldID (int& type, int& name, int offset) const
{
    if (!get8 (type, offset))
    {
        WriteLog (lsWARNING, Serializer) << "gFID: unable to get type";
        return false;
    }

    name = type & 15;
    type >>= 4;

    if (type == 0)
    {
        // uncommon type
        if (!get8 (type, ++offset))
            return false;

        if ((type == 0) || (type < 16))
        {
            WriteLog (lsWARNING, Serializer) << "gFID: uncommon type out of range " << type;
            return false;
        }
    }

    if (name == 0)
    {
        // uncommon name
        if (!get8 (name, ++offset))
            return false;

        if ((name == 0) || (name < 16))
        {
            WriteLog (lsWARNING, Serializer) << "gFID: uncommon name out of range " << name;
            return false;
        }
    }

    return true;
}

int Serializer::add8 (unsigned char byte)
{
    int ret = mData.size ();
    mData.push_back (byte);
    return ret;
}

bool Serializer::get8 (int& byte, int offset) const
{
    if (offset >= mData.size ()) return false;

    byte = mData[offset];
    return true;
}

bool Serializer::chop (int bytes)
{
    if (bytes > mData.size ()) return false;

    mData.resize (mData.size () - bytes);
    return true;
}

int Serializer::removeLastByte ()
{
    int size = mData.size () - 1;

    if (size < 0)
    {
        assert (false);
        return -1;
    }

    int ret = mData[size];
    mData.resize (size);
    return ret;
}

bool Serializer::getRaw (Blob& o, int offset, int length) const
{
    if ((offset + length) > mData.size ()) return false;

    o.assign (mData.begin () + offset, mData.begin () + offset + length);
    return true;
}

Blob Serializer::getRaw (int offset, int length) const
{
    Blob o;

    if ((offset + length) > mData.size ()) return o;

    o.assign (mData.begin () + offset, mData.begin () + offset + length);
    return o;
}

uint160 Serializer::getRIPEMD160 (int size) const
{
    uint160 ret;

    if ((size < 0) || (size > mData.size ())) size = mData.size ();

    RIPEMD160 (& (mData.front ()), size, (unsigned char*) &ret);
    return ret;
}

uint256 Serializer::getSHA256 (int size) const
{
    uint256 ret;

    if ((size < 0) || (size > mData.size ())) size = mData.size ();

    SHA256 (& (mData.front ()), size, (unsigned char*) &ret);
    return ret;
}

uint256 Serializer::getSHA512Half (int size) const
{
    assert (size != 0);
    if (size == 0)
        return uint256();
    if (size < 0 || size > mData.size())
        return ripple::getSHA512Half (mData);

    return ripple::getSHA512Half (
        mData.data(), size);
}

uint256 Serializer::getPrefixHash (std::uint32_t prefix, const unsigned char* data, int len)
{
    char be_prefix[4];
    be_prefix[0] = static_cast<unsigned char> (prefix >> 24);
    be_prefix[1] = static_cast<unsigned char> ((prefix >> 16) & 0xff);
    be_prefix[2] = static_cast<unsigned char> ((prefix >> 8) & 0xff);
    be_prefix[3] = static_cast<unsigned char> (prefix & 0xff);

    uint256 j[2];
    SHA512_CTX ctx;
    SHA512_Init (&ctx);
    SHA512_Update (&ctx, &be_prefix[0], 4);
    SHA512_Update (&ctx, data, len);
    SHA512_Final (reinterpret_cast<unsigned char*> (&j[0]), &ctx);

    return j[0];
}

int Serializer::addVL (Blob const& vector)
{
    int ret = addRaw (encodeVL (vector.size ()));
    addRaw (vector);
    assert (mData.size () == (ret + vector.size () + encodeLengthLength (vector.size ())));
    return ret;
}

int Serializer::addVL (const void* ptr, int len)
{
    int ret = addRaw (encodeVL (len));

    if (len)
        addRaw (ptr, len);

    return ret;
}

int Serializer::addVL (std::string const& string)
{
    int ret = addRaw (string.size ());

    if (!string.empty ())
        addRaw (string.data (), string.size ());

    return ret;
}

bool Serializer::getVL (Blob& objectVL, int offset, int& length) const
{
    int b1;

    if (!get8 (b1, offset++)) return false;

    int datLen, lenLen = decodeLengthLength (b1);

    try
    {
        if (lenLen == 1)
            datLen = decodeVLLength (b1);
        else if (lenLen == 2)
        {
            int b2;

            if (!get8 (b2, offset++)) return false;

            datLen = decodeVLLength (b1, b2);
        }
        else if (lenLen == 3)
        {
            int b2, b3;

            if (!get8 (b2, offset++)) return false;

            if (!get8 (b3, offset++)) return false;

            datLen = decodeVLLength (b1, b2, b3);
        }
        else return false;
    }
    catch (...)
    {
        return false;
    }

    length = lenLen + datLen;
    return getRaw (objectVL, offset, datLen);
}

bool Serializer::getVLLength (int& length, int offset) const
{
    int b1;

    if (!get8 (b1, offset++)) return false;

    int lenLen = decodeLengthLength (b1);

    try
    {
        if (lenLen == 1)
            length = decodeVLLength (b1);
        else if (lenLen == 2)
        {
            int b2;

            if (!get8 (b2, offset++)) return false;

            length = decodeVLLength (b1, b2);
        }
        else if (lenLen == 3)
        {
            int b2, b3;

            if (!get8 (b2, offset++)) return false;

            if (!get8 (b3, offset++)) return false;

            length = decodeVLLength (b1, b2, b3);
        }
        else return false;
    }
    catch (...)
    {
        return false;
    }

    return true;
}

Blob Serializer::encodeVL (int length)
{
    unsigned char lenBytes[4];

    if (length <= 192)
    {
        lenBytes[0] = static_cast<unsigned char> (length);
        return Blob (&lenBytes[0], &lenBytes[1]);
    }
    else if (length <= 12480)
    {
        length -= 193;
        lenBytes[0] = 193 + static_cast<unsigned char> (length >> 8);
        lenBytes[1] = static_cast<unsigned char> (length & 0xff);
        return Blob (&lenBytes[0], &lenBytes[2]);
    }
    else if (length <= 918744)
    {
        length -= 12481;
        lenBytes[0] = 241 + static_cast<unsigned char> (length >> 16);
        lenBytes[1] = static_cast<unsigned char> ((length >> 8) & 0xff);
        lenBytes[2] = static_cast<unsigned char> (length & 0xff);
        return Blob (&lenBytes[0], &lenBytes[3]);
    }
    else throw std::overflow_error ("lenlen");
}

int Serializer::encodeLengthLength (int length)
{
    if (length < 0) throw std::overflow_error ("len<0");

    if (length <= 192) return 1;

    if (length <= 12480) return 2;

    if (length <= 918744) return 3;

    throw std::overflow_error ("len>918744");
}

int Serializer::decodeLengthLength (int b1)
{
    if (b1 < 0) throw std::overflow_error ("b1<0");

    if (b1 <= 192) return 1;

    if (b1 <= 240) return 2;

    if (b1 <= 254) return 3;

    throw std::overflow_error ("b1>254");
}

int Serializer::decodeVLLength (int b1)
{
    if (b1 < 0) throw std::overflow_error ("b1<0");

    if (b1 > 254) throw std::overflow_error ("b1>254");

    return b1;
}

int Serializer::decodeVLLength (int b1, int b2)
{
    if (b1 < 193) throw std::overflow_error ("b1<193");

    if (b1 > 240) throw std::overflow_error ("b1>240");

    return 193 + (b1 - 193) * 256 + b2;
}

int Serializer::decodeVLLength (int b1, int b2, int b3)
{
    if (b1 < 241) throw std::overflow_error ("b1<241");

    if (b1 > 254) throw std::overflow_error ("b1>254");

    return 12481 + (b1 - 241) * 65536 + b2 * 256 + b3;
}

//------------------------------------------------------------------------------

SerialIter::SerialIter (void const* data,
        std::size_t size) noexcept
    : p_ (reinterpret_cast<
        std::uint8_t const*>(data))
    , remain_ (size)
{
}

void
SerialIter::reset() noexcept
{
    p_ -= used_;
    remain_ += used_;
    used_ = 0;
}

unsigned char
SerialIter::get8()
{
    if (remain_ < 1)
        throw std::runtime_error(
            "invalid SerialIter get8");
    unsigned char t = *p_;
    ++p_;
    ++used_;
    --remain_;
    return t;
}

std::uint16_t
SerialIter::get16()
{
    if (remain_ < 2)
        throw std::runtime_error(
            "invalid SerialIter get16");
    auto t = p_;
    p_ += 2;
    used_ += 2;
    remain_ -= 2;
    return
        (std::uint64_t(*t++) <<  8) +
         std::uint64_t(*t  );
}

std::uint32_t
SerialIter::get32()
{
    if (remain_ < 4)
        throw std::runtime_error(
            "invalid SerialIter get32");
    auto t = p_;
    p_ += 4;
    used_ += 4;
    remain_ -= 4;
    return
        (std::uint64_t(*t++) << 24) +
        (std::uint64_t(*t++) << 16) +
        (std::uint64_t(*t++) <<  8) +
         std::uint64_t(*t  );
}

std::uint64_t
SerialIter::get64 ()
{
    if (remain_ < 8)
        throw std::runtime_error(
            "invalid SerialIter get64");
    auto t = p_;
    p_ += 8;
    used_ += 8;
    remain_ -= 8;
    return
        (std::uint64_t(*t++) << 56) +
        (std::uint64_t(*t++) << 48) +
        (std::uint64_t(*t++) << 40) +
        (std::uint64_t(*t++) << 32) +
        (std::uint64_t(*t++) << 24) +
        (std::uint64_t(*t++) << 16) +
        (std::uint64_t(*t++) <<  8) +
         std::uint64_t(*t  );
}

void
SerialIter::getFieldID (int& type, int& name)
{
    type = get8();
    name = type & 15;
    type >>= 4;

    if (type == 0)
    {
        // uncommon type
        type = get8();
        if (type == 0 || type < 16)
            throw std::runtime_error(
                "gFID: uncommon type out of range " +
                    std::to_string(type));
    }

    if (name == 0)
    {
        // uncommon name
        name = get8();
        if (name == 0 || name < 16)
            throw std::runtime_error(
                "gFID: uncommon name out of range " +
                    std::to_string(name));
    }
}

// VFALCO DEPRECATED Returns a copy
Blob
SerialIter::getRaw (int size)
{
    if (remain_ < size)
        throw std::runtime_error(
            "invalid SerialIter getRaw");
    Blob b (p_, p_ + size);
    p_ += size;
    used_ += size;
    remain_ -= size;
    return b;

}

// VFALCO DEPRECATED Returns a copy
Blob
SerialIter::getVL()
{
    int b1 = get8();
    int datLen;
    int lenLen = Serializer::decodeLengthLength(b1);
    if (lenLen == 1)
    {
        datLen = Serializer::decodeVLLength (b1);
    }
    else if (lenLen == 2)
    {
        int b2 = get8();
        datLen = Serializer::decodeVLLength (b1, b2);
    }
    else
    {
        assert(lenLen == 3);
        int b2 = get8();
        int b3 = get8();
        datLen = Serializer::decodeVLLength (b1, b2, b3);
    }
    return getRaw(datLen);
}


//------------------------------------------------------------------------------

uint256
getSHA512Half (void const* data, int len)
{
    uint256 j[2];
    SHA512 (
        reinterpret_cast<unsigned char const*>(
            data), len, (unsigned char*) j);
    return j[0];
}

} // ripple
