#include <xrpl/nodestore/detail/codec.h>

#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/HashPrefix.h>

#include <gtest/gtest.h>
#include <nudb/detail/buffer.hpp>
#include <nudb/detail/stream.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace xrpl;
using namespace xrpl::node_store;

namespace {

// v1 inner-node layout: 16 hashes of 32 bytes each
constexpr std::size_t kHashCount = 16;
constexpr std::size_t kHashSize = 32;

std::vector<std::uint8_t>
makeInnerNode(std::size_t nonEmptySlots)
{
    using namespace nudb::detail;

    static constexpr std::size_t kInnerNodeSize = 525;

    std::array<std::uint8_t, kHashCount * kHashSize> hashes{};
    for (auto slot = 0uz; slot < nonEmptySlots; ++slot)
    {
        for (auto byte = 0uz; byte < kHashSize; ++byte)
        {
            std::size_t const offset = (slot * kHashSize) + byte;
            hashes[offset] = static_cast<std::uint8_t>((offset % 255) + 1);
        }
    }

    std::vector<std::uint8_t> blob(kInnerNodeSize);
    ostream os(blob.data(), blob.size());
    write<std::uint32_t>(os, 0);  // index
    write<std::uint32_t>(os, 0);  // unused
    write<std::uint8_t>(os, static_cast<std::uint8_t>(NodeObjectType::Unknown));
    write<std::uint32_t>(os, static_cast<std::uint32_t>(HashPrefix::InnerNode));
    write(os, hashes.data(), hashes.size());

    return blob;
}

std::uint8_t
codecType(std::pair<void const*, std::size_t> const& compressed)
{
    return static_cast<std::uint8_t const*>(compressed.first)[0];
}

}  // namespace

// All 16 hash slots populated - "full v1 inner node"
TEST(Codec, inner_node_full_roundtrip)
{
    static constexpr std::uint8_t kTypeInnerNodeFull = 3;

    auto const blob = makeInnerNode(kHashCount);

    nudb::detail::buffer compressBuf;
    auto const compressed = nodeobjectCompress(blob.data(), blob.size(), compressBuf);

    EXPECT_EQ(codecType(compressed), kTypeInnerNodeFull);
    EXPECT_EQ(compressed.second, sizeVarint(kTypeInnerNodeFull) + (kHashCount * kHashSize));

    nudb::detail::buffer decompressBuf;
    auto const restored = nodeobjectDecompress(compressed.first, compressed.second, decompressBuf);

    EXPECT_EQ(restored.second, blob.size());
    EXPECT_EQ(std::memcmp(restored.first, blob.data(), blob.size()), 0);
}

// Some hash slots empty - "compressed v1 inner node"
TEST(Codec, inner_node_compressed_roundtrip)
{
    static constexpr std::uint8_t kTypeInnerNodeCompressed = 2;
    static constexpr std::size_t kNonEmpty = 5;
    auto const blob = makeInnerNode(kNonEmpty);

    nudb::detail::buffer compressBuf;
    auto const compressed = nodeobjectCompress(blob.data(), blob.size(), compressBuf);

    EXPECT_EQ(codecType(compressed), kTypeInnerNodeCompressed);
    EXPECT_EQ(
        compressed.second,
        sizeVarint(kTypeInnerNodeCompressed) + sizeof(std::uint16_t) + (kNonEmpty * kHashSize));
    EXPECT_LT(compressed.second, blob.size());

    nudb::detail::buffer decompressBuf;
    auto const restored = nodeobjectDecompress(compressed.first, compressed.second, decompressBuf);

    EXPECT_EQ(restored.second, blob.size());
    EXPECT_EQ(std::memcmp(restored.first, blob.data(), blob.size()), 0);
}

// Anything that is not a v1 inner node - lz4 compressed
TEST(Codec, lz4_roundtrip)
{
    // A payload that is deliberately not a v1 inner node (any size other than 525), filled with a
    // short repeating pattern so lz4 actually shrinks it.
    static constexpr std::size_t kNonInnerNodeSize = 1000;
    static constexpr std::size_t kBytePatternPeriod = 7;
    static constexpr std::uint8_t kTypeLz4 = 1;

    std::vector<std::uint8_t> blob(kNonInnerNodeSize);
    for (auto i = 0uz; i < blob.size(); ++i)
        blob[i] = static_cast<std::uint8_t>(i % kBytePatternPeriod);

    nudb::detail::buffer compressBuf;
    auto const compressed = nodeobjectCompress(blob.data(), blob.size(), compressBuf);

    EXPECT_EQ(codecType(compressed), kTypeLz4);

    nudb::detail::buffer decompressBuf;
    auto const restored = nodeobjectDecompress(compressed.first, compressed.second, decompressBuf);

    EXPECT_EQ(restored.second, blob.size());
    EXPECT_EQ(std::memcmp(restored.first, blob.data(), blob.size()), 0);
}

// An uncompressed blob is never produced by the compressor but must still decode: leading varint 0
// followed by the raw payload.
TEST(Codec, uncompressed_passthrough)
{
    static constexpr std::uint8_t kTypeUncompressed = 0;
    static constexpr auto payload = std::to_array<std::uint8_t>({0xde, 0xad, 0xbe, 0xef, 0x2a});

    std::vector<std::uint8_t> blob;
    blob.push_back(kTypeUncompressed);  // leading varint type tag
    blob.insert(blob.end(), payload.begin(), payload.end());

    nudb::detail::buffer decompressBuf;
    auto const restored = nodeobjectDecompress(blob.data(), blob.size(), decompressBuf);

    EXPECT_EQ(restored.second, payload.size());
    EXPECT_EQ(std::memcmp(restored.first, payload.data(), payload.size()), 0);
}
