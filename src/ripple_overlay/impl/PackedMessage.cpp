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

PackedMessage::PackedMessage (::google::protobuf::Message const& message, int type)
{
    unsigned const messageBytes = message.ByteSize ();

    assert (messageBytes != 0);

    mBuffer.resize (kHeaderBytes + messageBytes);

    encodeHeader (messageBytes, type);

    if (messageBytes != 0)
    {
        message.SerializeToArray (&mBuffer [PackedMessage::kHeaderBytes], messageBytes);

#ifdef BEAST_DEBUG
        //Log::out() << "PackedMessage: type=" << type << ", datalen=" << msg_size;
#endif
    }
}

bool PackedMessage::operator== (PackedMessage const& other) const
{
    return mBuffer == other.mBuffer;
}

unsigned PackedMessage::getLength (std::vector <uint8_t> const& buf)
{
    unsigned result;

    if (buf.size () >= PackedMessage::kHeaderBytes)
    {
        result = buf [0];
        result <<= 8;
        result |= buf [1];
        result <<= 8;
        result |= buf [2];
        result <<= 8;
        result |= buf [3];
    }
    else
    {
        result = 0;
    }

    return result;
}

int PackedMessage::getType (std::vector<uint8_t> const& buf)
{
    if (buf.size () < PackedMessage::kHeaderBytes)
        return 0;

    int ret = buf[4];
    ret <<= 8;
    ret |= buf[5];
    return ret;
}

void PackedMessage::encodeHeader (unsigned size, int type)
{
    assert (mBuffer.size () >= PackedMessage::kHeaderBytes);
    mBuffer[0] = static_cast<boost::uint8_t> ((size >> 24) & 0xFF);
    mBuffer[1] = static_cast<boost::uint8_t> ((size >> 16) & 0xFF);
    mBuffer[2] = static_cast<boost::uint8_t> ((size >> 8) & 0xFF);
    mBuffer[3] = static_cast<boost::uint8_t> (size & 0xFF);
    mBuffer[4] = static_cast<boost::uint8_t> ((type >> 8) & 0xFF);
    mBuffer[5] = static_cast<boost::uint8_t> (type & 0xFF);
}

}
