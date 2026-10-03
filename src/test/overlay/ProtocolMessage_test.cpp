#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/ProtocolMessage.h>

#include <xrpl/beast/unit_test/suite.h>

#include <boost/asio/buffer.hpp>
#include <boost/system/errc.hpp>

#include <xrpl.pb.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class ProtocolMessage_test : public beast::unit_test::Suite
{
    struct TestHandler
    {
        bool compression = false;
        int beginCount = 0;
        int messageCount = 0;
        int endCount = 0;
        int unknownCount = 0;
        std::uint16_t lastType = 0;

        [[nodiscard]] bool
        compressionEnabled() const
        {
            return compression;
        }

        void
        onMessageUnknown(std::uint16_t type)
        {
            ++unknownCount;
            lastType = type;
        }

        void
        onMessageBegin(
            std::uint16_t type,
            std::shared_ptr<::google::protobuf::Message> const&,
            std::size_t,
            std::size_t,
            bool)
        {
            ++beginCount;
            lastType = type;
        }

        template <class T>
        void
        onMessage(std::shared_ptr<T> const&)
        {
            ++messageCount;
        }

        void
        onMessageEnd(std::uint16_t, std::shared_ptr<::google::protobuf::Message> const&)
        {
            ++endCount;
        }

        [[nodiscard]] static std::size_t
        maxManifestsMessageSize()
        {
            return std::numeric_limits<std::size_t>::max();
        }
    };

    // Wire bytes: `type` (2 bytes) + unknown field tag (2 bytes) + length varint (2 bytes, as these
    // tests all use unknownFieldSize >= 128).
    static constexpr std::size_t kPingProtoOverheadWithUnknownLen = 6;
    static constexpr std::size_t kMinimumPingSizeWithEmptyUnknownField =
        compression::kHeaderBytes + kPingProtoOverheadWithUnknownLen;
    static constexpr std::size_t kMinimumPingSizeCompressedWithEmptyUnknownField =
        compression::kHeaderBytesCompressed + kPingProtoOverheadWithUnknownLen;

    static std::vector<std::uint8_t>
    makePingBuffer(std::size_t unknownFieldSize, bool compressed = false)
    {
        auto ping = protocol::TMPing{};
        ping.set_type(protocol::TMPing::ptPING);
        if (unknownFieldSize > 0)
        {
            ping.mutable_unknown_fields()->AddLengthDelimited(
                42, std::string(unknownFieldSize, 'A'));
        }

        if (!compressed)
        {
            auto m = Message{ping, protocol::mtPING};
            return m.getBuffer(compression::Compressed::Off);
        }

        // Message::compress() refuses to compress pings (mtPING is not in its
        // allow-list), so getBuffer(Compressed::On) would just return the
        // uncompressed bytes. Roll it by hand here to get a compressed
        // ping message on the wire.
        auto payload = std::string{};
        ping.SerializeToString(&payload);

        auto deflated = std::vector<std::uint8_t>{};
        auto const deflatedSize = compression::compress(
            payload.data(),
            payload.size(),
            [&](std::size_t sz) {
                deflated.resize(sz);
                return deflated.data();
            },
            compression::Algorithm::LZ4);
        deflated.resize(deflatedSize);

        auto const type = static_cast<std::uint16_t>(protocol::mtPING);
        auto buffer = std::vector<std::uint8_t>{};
        auto pack = [&buffer](std::uint32_t value) {
            buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0x0F));
            buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
        };

        pack(static_cast<std::uint32_t>(deflated.size()));  // compressed payload size
        buffer.push_back(static_cast<std::uint8_t>((type >> 8) & 0xFF));
        buffer.push_back(static_cast<std::uint8_t>(type & 0xFF));
        pack(static_cast<std::uint32_t>(payload.size()));  // uncompressed size
        buffer[0] |= static_cast<std::uint8_t>(compression::Algorithm::LZ4);

        buffer.insert(buffer.end(), deflated.begin(), deflated.end());
        return buffer;
    }

    static std::optional<std::size_t>
    declaredPingSize(std::vector<std::uint8_t> const& buffer)
    {
        auto ec = boost::system::error_code{};
        auto const seq = std::array<boost::asio::const_buffer, 1>{boost::asio::buffer(buffer)};
        if (auto const header = xrpl::detail::parseMessageHeader(ec, seq, buffer.size()))
        {
            return header->uncompressedSize + header->headerSize;
        }
        return std::nullopt;
    }

    static std::pair<std::size_t, boost::system::error_code>
    invoke(std::vector<std::uint8_t> const& buffer, TestHandler& handler)
    {
        auto const seq = std::array<boost::asio::const_buffer, 1>{boost::asio::buffer(buffer)};
        auto hint = 0uz;
        return invokeProtocolMessage(seq, handler, hint);
    }

    void
    testOversizedPingRejected()
    {
        testcase("oversized ping rejected before dispatch");

        auto runLocalTest = [&](std::size_t size, bool compressed = false) {
            auto const buffer = makePingBuffer(size, compressed);
            auto const declared = declaredPingSize(buffer);
            if (BEAST_EXPECT(declared.has_value()))
                BEAST_EXPECT(*declared > kMaximumPingMessageSize);
            BEAST_EXPECT(buffer.size() < kMaximumMessageSize);

            auto handler = TestHandler{};
            handler.compression = compressed;
            auto const [bytes, ec] = invoke(buffer, handler);

            BEAST_EXPECT(ec == make_error_code(boost::system::errc::message_size));
            BEAST_EXPECT(bytes == 0);
            BEAST_EXPECT(handler.beginCount == 0);
            BEAST_EXPECT(handler.messageCount == 0);
            BEAST_EXPECT(handler.endCount == 0);
        };
        // Just over the cap, and comfortably over it.
        runLocalTest(kMaximumPingMessageSize + 1 - kMinimumPingSizeWithEmptyUnknownField);
        runLocalTest((2 * kMaximumPingMessageSize) - kMinimumPingSizeWithEmptyUnknownField);
        runLocalTest(
            kMaximumPingMessageSize + 1 - kMinimumPingSizeCompressedWithEmptyUnknownField, true);
        runLocalTest(
            (2 * kMaximumPingMessageSize) - kMinimumPingSizeCompressedWithEmptyUnknownField, true);
    }

    void
    testOversizedPingRejectedFromHeaderAlone()
    {
        testcase("oversized ping rejected from header alone");

        auto runLocalTest = [&](std::size_t size, bool compressed = false) {
            auto const full = makePingBuffer(size, compressed);
            auto const headerSize =
                compressed ? compression::kHeaderBytesCompressed : compression::kHeaderBytes;

            // Only the header has arrived; the declared payload is still in flight.
            auto const headerOnly =
                std::vector<std::uint8_t>{full.begin(), full.begin() + headerSize};
            BEAST_EXPECT(headerOnly.size() < full.size());

            auto handler = TestHandler{};
            handler.compression = compressed;
            auto const [bytes, ec] = invoke(headerOnly, handler);

            BEAST_EXPECT(ec == make_error_code(boost::system::errc::message_size));
            BEAST_EXPECT(bytes == 0);
            BEAST_EXPECT(handler.beginCount == 0);
            BEAST_EXPECT(handler.messageCount == 0);
            BEAST_EXPECT(handler.endCount == 0);
        };
        runLocalTest(kMaximumPingMessageSize + 1 - kMinimumPingSizeWithEmptyUnknownField);
        runLocalTest((2 * kMaximumPingMessageSize) - kMinimumPingSizeWithEmptyUnknownField);
        runLocalTest(
            kMaximumPingMessageSize + 1 - kMinimumPingSizeCompressedWithEmptyUnknownField, true);
        runLocalTest(
            (2 * kMaximumPingMessageSize) - kMinimumPingSizeCompressedWithEmptyUnknownField, true);
    }

    void
    testNormalPingDispatched()
    {
        testcase("normal ping dispatched");

        auto runLocalTest = [&](std::size_t size, bool compressed = false) {
            auto const buffer = makePingBuffer(size, compressed);
            auto const declared = declaredPingSize(buffer);
            if (BEAST_EXPECT(declared.has_value()))
                BEAST_EXPECT(*declared <= kMaximumPingMessageSize);

            auto handler = TestHandler{};
            handler.compression = compressed;
            auto const [bytes, ec] = invoke(buffer, handler);

            BEAST_EXPECT(!ec);
            BEAST_EXPECT(bytes == buffer.size());
            BEAST_EXPECT(handler.beginCount == 1);
            BEAST_EXPECT(handler.messageCount == 1);
            BEAST_EXPECT(handler.endCount == 1);
        };
        runLocalTest(0);
        runLocalTest(0, true);
    }

    void
    testPingWithSmallUnknownFieldDispatched()
    {
        testcase("ping with small unknown field still dispatched");

        auto runLocalTest = [&](std::size_t size, bool compressed = false) {
            auto const buffer = makePingBuffer(size, compressed);
            auto const declared = declaredPingSize(buffer);
            if (BEAST_EXPECT(declared.has_value()))
                BEAST_EXPECT(*declared <= kMaximumPingMessageSize);

            auto handler = TestHandler{};
            handler.compression = compressed;
            auto const [bytes, ec] = invoke(buffer, handler);

            BEAST_EXPECT(!ec);
            BEAST_EXPECT(bytes == buffer.size());
            BEAST_EXPECT(handler.beginCount == 1);
            BEAST_EXPECT(handler.messageCount == 1);
            BEAST_EXPECT(handler.endCount == 1);
        };
        // Well under the cap, one byte under it, and exactly at it.
        runLocalTest((kMaximumPingMessageSize / 2) - kMinimumPingSizeWithEmptyUnknownField);
        runLocalTest(kMaximumPingMessageSize - 1 - kMinimumPingSizeWithEmptyUnknownField);
        runLocalTest(kMaximumPingMessageSize - kMinimumPingSizeWithEmptyUnknownField);
        runLocalTest(
            (kMaximumPingMessageSize / 2) - kMinimumPingSizeCompressedWithEmptyUnknownField, true);
        runLocalTest(
            kMaximumPingMessageSize - 1 - kMinimumPingSizeCompressedWithEmptyUnknownField, true);
        runLocalTest(
            kMaximumPingMessageSize - kMinimumPingSizeCompressedWithEmptyUnknownField, true);
    }

    void
    run() override
    {
        testOversizedPingRejected();
        testOversizedPingRejectedFromHeaderAlone();
        testNormalPingDispatched();
        testPingWithSmallUnknownFieldDispatched();
    }
};

BEAST_DEFINE_TESTSUITE(ProtocolMessage, overlay, xrpl);

}  // namespace xrpl::test
