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

#ifndef RIPPLE_SERIALIZER_H
#define RIPPLE_SERIALIZER_H

#include <iomanip>

#include <ripple/common/byte_view.h>

namespace ripple {

class CKey; // forward declaration

class Serializer
{
public:
    typedef std::shared_ptr<Serializer> pointer;

protected:
    Blob mData;

public:
    Serializer (int n = 256)
    {
        mData.reserve (n);
    }
    Serializer (Blob const& data) : mData (data)
    {
        ;
    }
    Serializer (std::string const& data) : mData (data.data (), (data.data ()) + data.size ())
    {
        ;
    }
    Serializer (Blob ::iterator begin, Blob ::iterator end) :
        mData (begin, end)
    {
        ;
    }
    Serializer (Blob ::const_iterator begin, Blob ::const_iterator end) :
        mData (begin, end)
    {
        ;
    }

    // assemble functions
    int add8 (unsigned char byte);
    int add16 (std::uint16_t);
    int add32 (std::uint32_t);      // ledger indexes, account sequence, timestamps
    int add64 (std::uint64_t);      // native currency amounts
    int add128 (const uint128&);    // private key generators
    int add256 (uint256 const& );       // transaction and ledger hashes

    template <typename Integer>
    int addInteger(Integer);

    template <int Bits, class Tag>
    int addBitString(base_uint<Bits, Tag> const& v) {
        int ret = mData.size ();
        mData.insert (mData.end (), v.begin (), v.end ());
        return ret;
    }

    // TODO(tom): merge with add128 and add256.
    template <class Tag>
    int add160 (base_uint<160, Tag> const& i)
    {
        return addBitString<160, Tag>(i);
    }

    int addRaw (Blob const& vector);
    int addRaw (const void* ptr, int len);
    int addRaw (const Serializer& s);
    int addZeros (size_t uBytes);

    int addVL (Blob const& vector);
    int addVL (std::string const& string);
    int addVL (const void* ptr, int len);

    // disassemble functions
    bool get8 (int&, int offset) const;
    bool get8 (unsigned char&, int offset) const;
    bool get16 (std::uint16_t&, int offset) const;
    bool get32 (std::uint32_t&, int offset) const;
    bool get64 (std::uint64_t&, int offset) const;
    bool get128 (uint128&, int offset) const;
    bool get256 (uint256&, int offset) const;

    template <typename Integer>
    bool getInteger(Integer& number, int offset) {
        static const auto bytes = sizeof(Integer);
        if ((offset + bytes) > mData.size ())
            return false;
        number = 0;

        auto ptr = &mData[offset];
        for (auto i = 0; i < bytes; ++i)
        {
            if (i)
                number <<= 8;
            number |= *ptr++;
        }
        return true;
    }

    template <int Bits, typename Tag = void>
    bool getBitString(base_uint<Bits, Tag>& data, int offset) const {
        auto success = (offset + (Bits / 8)) <= mData.size ();
        if (success)
            memcpy (data.begin (), & (mData.front ()) + offset, (Bits / 8));
        return success;
    }

    uint256 get256 (int offset) const;

    // TODO(tom): merge with get128 and get256.
    template <class Tag>
    bool get160 (base_uint<160, Tag>& o, int offset) const
    {
        return getBitString<160, Tag>(o, offset);
    }

    bool getRaw (Blob&, int offset, int length) const;
    Blob getRaw (int offset, int length) const;

    bool getVL (Blob& objectVL, int offset, int& length) const;
    bool getVLLength (int& length, int offset) const;

    bool getFieldID (int& type, int& name, int offset) const;
    int addFieldID (int type, int name);
    int addFieldID (SerializedTypeID type, int name)
    {
        return addFieldID (static_cast<int> (type), name);
    }

    // normal hash functions
    uint160 getRIPEMD160 (int size = -1) const;
    uint256 getSHA256 (int size = -1) const;
    uint256 getSHA512Half (int size = -1) const;
    static uint256 getSHA512Half (const_byte_view v);

    static uint256 getSHA512Half (const unsigned char* data, int len);

    // prefix hash functions
    static uint256 getPrefixHash (std::uint32_t prefix, const unsigned char* data, int len);
    uint256 getPrefixHash (std::uint32_t prefix) const
    {
        return getPrefixHash (prefix, & (mData.front ()), mData.size ());
    }
    static uint256 getPrefixHash (std::uint32_t prefix, Blob const& data)
    {
        return getPrefixHash (prefix, & (data.front ()), data.size ());
    }
    static uint256 getPrefixHash (std::uint32_t prefix, std::string const& strData)
    {
        return getPrefixHash (prefix, reinterpret_cast<const unsigned char*> (strData.data ()), strData.size ());
    }

    // totality functions
    Blob const& peekData () const
    {
        return mData;
    }
    Blob getData () const
    {
        return mData;
    }
    Blob& modData ()
    {
        return mData;
    }
    int getCapacity () const
    {
        return mData.capacity ();
    }
    int getDataLength () const
    {
        return mData.size ();
    }
    const void* getDataPtr () const
    {
        return &mData.front ();
    }
    void* getDataPtr ()
    {
        return &mData.front ();
    }
    int getLength () const
    {
        return mData.size ();
    }
    std::string getString () const
    {
        return std::string (static_cast<const char*> (getDataPtr ()), size ());
    }
    void secureErase ()
    {
        memset (& (mData.front ()), 0, mData.size ());
        erase ();
    }
    void erase ()
    {
        mData.clear ();
    }
    int removeLastByte ();
    bool chop (int num);

    // vector-like functions
    Blob ::iterator begin ()
    {
        return mData.begin ();
    }
    Blob ::iterator end ()
    {
        return mData.end ();
    }
    Blob ::const_iterator begin () const
    {
        return mData.begin ();
    }
    Blob ::const_iterator end () const
    {
        return mData.end ();
    }
    Blob ::size_type size () const
    {
        return mData.size ();
    }
    void reserve (size_t n)
    {
        mData.reserve (n);
    }
    void resize (size_t n)
    {
        mData.resize (n);
    }
    size_t capacity () const
    {
        return mData.capacity ();
    }

    bool operator== (Blob const& v)
    {
        return v == mData;
    }
    bool operator!= (Blob const& v)
    {
        return v != mData;
    }
    bool operator== (const Serializer& v)
    {
        return v.mData == mData;
    }
    bool operator!= (const Serializer& v)
    {
        return v.mData != mData;
    }

    std::string getHex () const
    {
        std::stringstream h;

        for (unsigned char const& element : mData)
        {
            h <<
                std::setw (2) <<
                std::hex <<
                std::setfill ('0') <<
                static_cast<unsigned int>(element);
        }
        return h.str ();
    }

    // low-level VL length encode/decode functions
    static Blob encodeVL (int length);
    static int lengthVL (int length)
    {
        return length + encodeLengthLength (length);
    }
    static int encodeLengthLength (int length); // length to encode length
    static int decodeLengthLength (int b1);
    static int decodeVLLength (int b1);
    static int decodeVLLength (int b1, int b2);
    static int decodeVLLength (int b1, int b2, int b3);

    static void TestSerializer ();
};

class SerializerIterator
{
protected:
    const Serializer& mSerializer;
    int mPos;

public:

    // Reference is not const because we don't want to bind to a temporary
    SerializerIterator (Serializer& s) : mSerializer (s), mPos (0)
    {
        ;
    }

    const Serializer& operator* (void)
    {
        return mSerializer;
    }
    void reset (void)
    {
        mPos = 0;
    }
    void setPos (int p)
    {
        mPos = p;
    }

    int getPos (void)
    {
        return mPos;
    }
    bool empty ()
    {
        return mPos == mSerializer.getLength ();
    }
    int getBytesLeft ();

    // get functions throw on error
    unsigned char get8 ();
    std::uint16_t get16 ();
    std::uint32_t get32 ();
    std::uint64_t get64 ();

    uint128 get128 () { return getBitString<128>(); }
    uint160 get160 () { return getBitString<160>(); }
    uint256 get256 () { return getBitString<256>(); }

    template <std::size_t Bits, typename Tag = void>
    void getBitString (base_uint<Bits, Tag>& bits) {
        if (!mSerializer.getBitString<Bits> (bits, mPos))
            throw std::runtime_error ("invalid serializer getBitString");

        mPos += Bits / 8;
    }

    template <std::size_t Bits, typename Tag = void>
    base_uint<Bits, Tag> getBitString () {
        base_uint<Bits, Tag> bits;
        getBitString(bits);
        return bits;
    }

    void getFieldID (int& type, int& field);

    Blob getRaw (int iLength);

    Blob getVL ();
};

} // ripple

#endif
