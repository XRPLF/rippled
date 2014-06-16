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

namespace ripple {

SETUP_LOG (Serializer)

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

int Serializer::add128 (const uint128& i)
{
    int ret = mData.size ();
    mData.insert (mData.end (), i.begin (), i.end ());
    return ret;
}

int Serializer::add160 (const uint160& i)
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

bool Serializer::get160 (uint160& o, int offset) const
{
    if ((offset + (160 / 8)) > mData.size ()) return false;

    memcpy (o.begin (), & (mData.front ()) + offset, (160 / 8));
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
        return getSHA512Half (mData);

    return getSHA512Half (const_byte_view (
        mData.data(), mData.data() + size));
}

uint256 Serializer::getSHA512Half (const_byte_view v)
{
    uint256 j[2];
    SHA512 (v.data(), v.size(),
        reinterpret_cast<unsigned char*> (j));
    return j[0];
}

uint256 Serializer::getSHA512Half (const unsigned char* data, int len)
{
    uint256 j[2];
    SHA512 (data, len, (unsigned char*) j);
    return j[0];
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

int Serializer::addVL (const std::string& string)
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

void Serializer::TestSerializer ()
{
    Serializer s (64);
}

int SerializerIterator::getBytesLeft ()
{
    return mSerializer.size () - mPos;
}

void SerializerIterator::getFieldID (int& type, int& field)
{
    if (!mSerializer.getFieldID (type, field, mPos))
        throw std::runtime_error ("invalid serializer getFieldID");

    ++mPos;

    if (type >= 16)
        ++mPos;

    if (field >= 16)
        ++mPos;
}

unsigned char SerializerIterator::get8 ()
{
    int val;

    if (!mSerializer.get8 (val, mPos)) throw std::runtime_error ("invalid serializer get8");

    ++mPos;
    return val;
}

std::uint16_t SerializerIterator::get16 ()
{
    std::uint16_t val;

    if (!mSerializer.get16 (val, mPos)) throw std::runtime_error ("invalid serializer get16");

    mPos += 16 / 8;
    return val;
}

std::uint32_t SerializerIterator::get32 ()
{
    std::uint32_t val;

    if (!mSerializer.get32 (val, mPos)) throw std::runtime_error ("invalid serializer get32");

    mPos += 32 / 8;
    return val;
}

std::uint64_t SerializerIterator::get64 ()
{
    std::uint64_t val;

    if (!mSerializer.get64 (val, mPos)) throw std::runtime_error ("invalid serializer get64");

    mPos += 64 / 8;
    return val;
}

uint128 SerializerIterator::get128 ()
{
    uint128 val;

    if (!mSerializer.get128 (val, mPos)) throw std::runtime_error ("invalid serializer get128");

    mPos += 128 / 8;
    return val;
}

uint160 SerializerIterator::get160 ()
{
    uint160 val;

    if (!mSerializer.get160 (val, mPos)) throw std::runtime_error ("invalid serializer get160");

    mPos += 160 / 8;
    return val;
}

uint256 SerializerIterator::get256 ()
{
    uint256 val;

    if (!mSerializer.get256 (val, mPos)) throw std::runtime_error ("invalid serializer get256");

    mPos += 256 / 8;
    return val;
}

Blob SerializerIterator::getVL ()
{
    int length;
    Blob vl;

    if (!mSerializer.getVL (vl, mPos, length)) throw std::runtime_error ("invalid serializer getVL");

    mPos += length;
    return vl;
}

Blob SerializerIterator::getRaw (int iLength)
{
    int iPos    = mPos;
    mPos        += iLength;

    return mSerializer.getRaw (iPos, iLength);
}

//------------------------------------------------------------------------------

class Serializer_test : public beast::unit_test::suite
{
public:
    void run ()
    {
        Serializer s1;
        s1.add32 (3);
        s1.add256 (uint256 ());

        Serializer s2;
        s2.add32 (0x12345600);
        s2.addRaw (s1.peekData ());

        expect (s1.getPrefixHash (0x12345600) == s2.getSHA512Half ());
    }
};

BEAST_DEFINE_TESTSUITE(Serializer,ripple_data,ripple);

} // ripple
