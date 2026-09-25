#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/ProtocolMessage.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/telemetry/TraceContextValidation.h>

#include <boost/asio/buffer.hpp>
#include <boost/system/errc.hpp>

#include <xrpl.pb.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
        // The last message dispatched, as the parser produced it.
        std::shared_ptr<::google::protobuf::Message> lastMessage;

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
        onMessage(std::shared_ptr<T> const& m)
        {
            ++messageCount;
            lastMessage = m;
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

    // Stand-in bytes for the required bytes fields of the messages below.
    static constexpr std::string_view kBlob = "blob";

    // The TraceContext field number reserved for trace_state in xrpl.proto.
    static constexpr int kReservedTraceStateField = 4;

    // A payload size well over the one below which Message sends a frame
    // uncompressed. parseFromPeer checks the frame really is compressed.
    static constexpr std::size_t kCompressiblePayloadSize = 1024;

    // Bytes 1, 2, 3, ... of the given size, so every byte is non-zero.
    static std::string
    sequenceBytes(std::size_t size)
    {
        auto bytes = std::string(size, '\0');
        std::ranges::generate(bytes, [next = '\0']() mutable { return ++next; });
        return bytes;
    }

    // A trace context that passes every check.
    static protocol::TraceContext
    validTraceContext()
    {
        auto tc = protocol::TraceContext{};
        tc.set_trace_id(sequenceBytes(telemetry::kTraceIdSize));
        tc.set_span_id(sequenceBytes(telemetry::kSpanIdSize));
        return tc;
    }

    static protocol::TMValidation
    validationWith(protocol::TraceContext const& tc)
    {
        auto validation = protocol::TMValidation{};
        validation.set_validation(kBlob);
        *validation.mutable_trace_context() = tc;
        return validation;
    }

    static protocol::TMTransaction
    transactionWith(protocol::TraceContext const& tc)
    {
        auto tx = protocol::TMTransaction{};
        tx.set_rawtransaction(kBlob);
        tx.set_status(protocol::tsNEW);
        *tx.mutable_trace_context() = tc;
        return tx;
    }

    // Sends `message` through the parser as a peer would. Returns the
    // message the handler got, or null if it is not a T.
    template <class T>
    std::shared_ptr<T>
    parseFromPeer(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        compression::Compressed compressed = compression::Compressed::Off)
    {
        auto framed = Message{message, type};
        auto const& buffer = framed.getBuffer(compressed);
        if (compressed == compression::Compressed::On)
        {
            // Message falls back to an uncompressed frame when compressing
            // does not help, so confirm the header says LZ4.
            auto headerEc = boost::system::error_code{};
            auto const seq = std::array<boost::asio::const_buffer, 1>{boost::asio::buffer(buffer)};
            auto const header = xrpl::detail::parseMessageHeader(headerEc, seq, buffer.size());
            BEAST_EXPECT(header.has_value() && header->algorithm == compression::Algorithm::LZ4);
        }

        auto handler = TestHandler{};
        handler.compression = compressed == compression::Compressed::On;
        auto const [bytes, ec] = invoke(buffer, handler);
        BEAST_EXPECT(!ec);
        BEAST_EXPECT(bytes == buffer.size());
        BEAST_EXPECT(handler.messageCount == 1);
        return std::dynamic_pointer_cast<T>(handler.lastMessage);
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
    testPeerTraceContextSanitized()
    {
        testcase("peer trace context sanitized on parse");

        // A trace_id of the wrong size drops the whole context.
        {
            auto tc = validTraceContext();
            tc.set_trace_id(sequenceBytes(telemetry::kTraceIdSize - 1));
            auto const parsed =
                parseFromPeer<protocol::TMValidation>(validationWith(tc), protocol::mtVALIDATION);
            if (BEAST_EXPECT(parsed != nullptr))
            {
                BEAST_EXPECT(!parsed->has_trace_context());
                BEAST_EXPECT(parsed->validation() == kBlob);
            }
        }

        // A valid context is kept, with its unknown flag bits cleared.
        {
            auto tc = validTraceContext();
            tc.set_trace_flags(telemetry::kMaxTraceFlags);
            auto const parsed =
                parseFromPeer<protocol::TMValidation>(validationWith(tc), protocol::mtVALIDATION);
            if (BEAST_EXPECT(parsed != nullptr) && BEAST_EXPECT(parsed->has_trace_context()))
            {
                auto const& kept = parsed->trace_context();
                BEAST_EXPECT(kept.trace_id() == tc.trace_id());
                BEAST_EXPECT(kept.span_id() == tc.span_id());
                BEAST_EXPECT(kept.trace_flags() == telemetry::kKnownTraceFlags);
            }
        }

        // In a batch, only the transaction with the bad context loses it.
        {
            auto bad = validTraceContext();
            bad.set_span_id(std::string(telemetry::kSpanIdSize, '\0'));
            auto good = validTraceContext();
            good.set_trace_flags(telemetry::kMaxTraceFlags);
            auto batch = protocol::TMTransactions{};
            *batch.add_transactions() = transactionWith(bad);
            *batch.add_transactions() = transactionWith(good);

            auto const parsed =
                parseFromPeer<protocol::TMTransactions>(batch, protocol::mtTRANSACTIONS);
            if (BEAST_EXPECT(parsed != nullptr) && BEAST_EXPECT(parsed->transactions_size() == 2))
            {
                BEAST_EXPECT(!parsed->transactions(0).has_trace_context());
                auto const& kept = parsed->transactions(1);
                BEAST_EXPECT(kept.has_trace_context());
                BEAST_EXPECT(kept.trace_context().trace_id() == good.trace_id());
                BEAST_EXPECT(kept.trace_context().span_id() == good.span_id());
                BEAST_EXPECT(kept.trace_context().trace_flags() == telemetry::kKnownTraceFlags);
            }
        }
    }

    void
    testPeerTraceContextUnknownFieldDropped()
    {
        testcase("unknown field in peer trace context dropped on parse");

        auto const tc = validTraceContext();
        auto validation = validationWith(tc);
        validation.mutable_trace_context()->mutable_unknown_fields()->AddLengthDelimited(
            kReservedTraceStateField, "state");
        BEAST_EXPECT(validation.trace_context().unknown_fields().field_count() == 1);

        auto const parsed =
            parseFromPeer<protocol::TMValidation>(validation, protocol::mtVALIDATION);
        if (BEAST_EXPECT(parsed != nullptr) && BEAST_EXPECT(parsed->has_trace_context()))
        {
            auto const& kept = parsed->trace_context();
            BEAST_EXPECT(kept.trace_id() == tc.trace_id());
            BEAST_EXPECT(kept.span_id() == tc.span_id());
            BEAST_EXPECT(kept.unknown_fields().field_count() == 0);
        }
    }

    void
    testPeerTraceContextSanitizedOnProposal()
    {
        testcase("peer trace context sanitized on a proposal");

        auto tc = validTraceContext();
        tc.set_trace_id(std::string(telemetry::kTraceIdSize, '\0'));
        auto proposal = protocol::TMProposeSet{};
        proposal.set_proposeseq(1);
        proposal.set_currenttxhash(kBlob);
        proposal.set_nodepubkey(kBlob);
        proposal.set_closetime(1);
        proposal.set_signature(kBlob);
        proposal.set_previousledger(kBlob);
        *proposal.mutable_trace_context() = tc;
        BEAST_EXPECT(proposal.IsInitialized());

        // Everything but the trace context must arrive unchanged.
        auto expected = proposal;
        expected.clear_trace_context();

        auto const parsed =
            parseFromPeer<protocol::TMProposeSet>(proposal, protocol::mtPROPOSE_LEDGER);
        if (BEAST_EXPECT(parsed != nullptr))
        {
            BEAST_EXPECT(!parsed->has_trace_context());
            BEAST_EXPECT(parsed->SerializeAsString() == expected.SerializeAsString());
        }
    }

    void
    testPeerTraceContextSanitizedWhenCompressed()
    {
        testcase("peer trace context sanitized on a compressed message");

        auto bad = validTraceContext();
        bad.set_span_id(std::string(telemetry::kSpanIdSize, '\0'));
        auto tx = transactionWith(bad);
        tx.set_rawtransaction(std::string(kCompressiblePayloadSize, 'A'));

        auto const parsed = parseFromPeer<protocol::TMTransaction>(
            tx, protocol::mtTRANSACTION, compression::Compressed::On);
        if (BEAST_EXPECT(parsed != nullptr))
        {
            BEAST_EXPECT(!parsed->has_trace_context());
            BEAST_EXPECT(parsed->rawtransaction() == tx.rawtransaction());
        }
    }

    void
    run() override
    {
        testOversizedPingRejected();
        testOversizedPingRejectedFromHeaderAlone();
        testNormalPingDispatched();
        testPingWithSmallUnknownFieldDispatched();
        testPeerTraceContextSanitized();
        testPeerTraceContextUnknownFieldDropped();
        testPeerTraceContextSanitizedOnProposal();
        testPeerTraceContextSanitizedWhenCompressed();
    }
};

BEAST_DEFINE_TESTSUITE(ProtocolMessage, overlay, xrpl);

}  // namespace xrpl::test
