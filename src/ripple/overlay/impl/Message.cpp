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

#include <ripple/overlay/Message.h>
#include <ripple/overlay/impl/TrafficCount.h>
#include <cstdint>

namespace ripple {

Message::Message(::google::protobuf::Message const& message, int type)
    : category_(TrafficCount::categorize(message, type, false))
{
    using namespace ripple::compression;

#if defined(GOOGLE_PROTOBUF_VERSION) && (GOOGLE_PROTOBUF_VERSION >= 3011000)
    auto const messageBytes = message.ByteSizeLong();
#else
    unsigned const messageBytes = message.ByteSize();
#endif

    assert(messageBytes != 0);

    buffer_.resize(headerBytes + messageBytes);

    setHeader(buffer_.data(), messageBytes, type, Algorithm::None, 0);

    if (messageBytes != 0)
        message.SerializeToArray(buffer_.data() + headerBytes, messageBytes);
}

void
Message::compress()
{
    using namespace ripple::compression;
    auto const messageBytes = buffer_.size() - headerBytes;

    auto type = getType(buffer_.data());

    bool const compressible = [&] {
        if (messageBytes <= 70)
            return false;
        switch (type)
        {
            case protocol::mtMANIFESTS:
            case protocol::mtENDPOINTS:
            case protocol::mtTRANSACTION:
            case protocol::mtGET_LEDGER:
            case protocol::mtLEDGER_DATA:
            case protocol::mtGET_OBJECTS:
            case protocol::mtVALIDATORLIST:
                return true;
            case protocol::mtPING:
            case protocol::mtCLUSTER:
            case protocol::mtPROPOSE_LEDGER:
            case protocol::mtSTATUS_CHANGE:
            case protocol::mtHAVE_SET:
            case protocol::mtVALIDATION:
            case protocol::mtGET_SHARD_INFO:
            case protocol::mtSHARD_INFO:
            case protocol::mtGET_PEER_SHARD_INFO:
            case protocol::mtPEER_SHARD_INFO:
                break;
        }
        return false;
    }();

    if (compressible)
    {
        auto payload = static_cast<void const*>(buffer_.data() + headerBytes);

        auto compressedSize = ripple::compression::compress(
            payload,
            messageBytes,
            [&](std::size_t inSize) {  // size of required compressed buffer
                bufferCompressed_.resize(inSize + headerBytesCompressed);
                return (bufferCompressed_.data() + headerBytesCompressed);
            });

        if (compressedSize <
            (messageBytes - (headerBytesCompressed - headerBytes)))
        {
            bufferCompressed_.resize(headerBytesCompressed + compressedSize);
            setHeader(
                bufferCompressed_.data(),
                compressedSize,
                type,
                Algorithm::LZ4,
                messageBytes);
        }
        else
            bufferCompressed_.resize(0);
    }
}

/** Set payload header
 * Uncompressed message header
 * 47-42    Set to 0
 * 41-16    Payload size
 * 15-0	    Message Type
 * Compressed message header
 * 79       Set to 0, indicates the message is compressed
 * 78-76    Compression algorithm, value 1-7. Set to 1 to indicate LZ4
 * compression 75-74    Set to 0 73-48    Payload size 47-32	Message Type
 * 31-0     Uncompressed message size
 */
void
Message::setHeader(
    std::uint8_t* in,
    std::uint32_t payloadBytes,
    int type,
    Algorithm comprAlgorithm,
    std::uint32_t uncompressedBytes)
{
    auto h = in;

    auto pack = [](std::uint8_t*& in, std::uint32_t size) {
        *in++ = static_cast<std::uint8_t>(
            (size >> 24) & 0x0F);  // leftmost 4 are compression bits
        *in++ = static_cast<std::uint8_t>((size >> 16) & 0xFF);
        *in++ = static_cast<std::uint8_t>((size >> 8) & 0xFF);
        *in++ = static_cast<std::uint8_t>(size & 0xFF);
    };

    pack(in, payloadBytes);

    *in++ = static_cast<std::uint8_t>((type >> 8) & 0xFF);
    *in++ = static_cast<std::uint8_t>(type & 0xFF);

    if (comprAlgorithm != Algorithm::None)
    {
        pack(in, uncompressedBytes);
        *h |= 0x80 | (static_cast<uint8_t>(comprAlgorithm) << 4);
    }
}

std::vector<uint8_t> const&
Message::getBuffer(Compressed tryCompressed)
{
    if (tryCompressed == Compressed::Off)
        return buffer_;

    std::call_once(once_flag_, &Message::compress, this);

    if (bufferCompressed_.size() > 0)
        return bufferCompressed_;
    else
        return buffer_;
}

int
Message::getType(std::uint8_t const* in) const
{
    int type = (static_cast<int>(*(in + 4)) << 8) + *(in + 5);
    return type;
}

}  // namespace ripple
