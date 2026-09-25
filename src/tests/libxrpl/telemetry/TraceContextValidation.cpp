#include <xrpl/telemetry/TraceContextValidation.h>

#include <xrpl/proto/xrpl.pb.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/unknown_field_set.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

/**
 * Tests for the checks run on a trace context received from a peer.
 *
 * Every size and flag limit is read from the production constants, so a
 * change to a limit moves the boundary these tests probe.
 */

using namespace xrpl::telemetry;

namespace {

// Stand-in bytes for the required bytes fields of the messages below.
constexpr std::string_view kBlob = "blob";

// The W3C sampled bit of trace_flags.
constexpr std::uint32_t kSampledBit = 0x01;

// The W3C random bit of trace_flags (Trace Context Level 2).
constexpr std::uint32_t kRandomBit = 0x02;

// A trace_flags bit W3C leaves undefined.
constexpr std::uint32_t kUndefinedBit = 0x04;

// The TraceContext field number and name reserved for trace_state in xrpl.proto.
constexpr int kReservedTraceStateField = 4;
constexpr std::string_view kReservedTraceStateName = "trace_state";

/**
 * Returns bytes 1, 2, 3, ... of the given size.
 *
 * Every byte is non-zero, so an id built from it fails a check only
 * because of its size.
 */
std::string
sequenceBytes(std::size_t size)
{
    auto bytes = std::string(size, '\0');
    std::ranges::generate(bytes, [next = '\0']() mutable { return ++next; });
    return bytes;
}

// A zero-filled id of the given size with only its last byte set.
std::string
lastByteSet(std::size_t size)
{
    auto id = std::string(size, '\0');
    id.back() = '\x01';
    return id;
}

// A context that passes every check: well-formed ids and no flags.
protocol::TraceContext
validContext()
{
    protocol::TraceContext tc;
    tc.set_trace_id(sequenceBytes(kTraceIdSize));
    tc.set_span_id(sequenceBytes(kSpanIdSize));
    return tc;
}

protocol::TMValidation
validationWith(protocol::TraceContext const& tc)
{
    protocol::TMValidation msg;
    msg.set_validation(kBlob);
    *msg.mutable_trace_context() = tc;
    return msg;
}

protocol::TMTransaction
transactionWith(protocol::TraceContext const& tc)
{
    protocol::TMTransaction tx;
    tx.set_rawtransaction(kBlob);
    tx.set_status(protocol::tsNEW);
    *tx.mutable_trace_context() = tc;
    return tx;
}

// trace_flags as an optional, so presence and value compare in one step.
std::optional<std::uint32_t>
flagsOf(protocol::TraceContext const& tc)
{
    if (!tc.has_trace_flags())
        return std::nullopt;
    return tc.trace_flags();
}

// The forms an id can take on the wire. SCOPED_TRACE prints the index.
enum class IdForm { Absent, Valid, WrongSize, AllZero };

constexpr std::array kIdForms{IdForm::Absent, IdForm::Valid, IdForm::WrongSize, IdForm::AllZero};

// The bytes of an id of the given form, or nullopt to leave the field unset.
std::optional<std::string>
idBytes(IdForm form, std::size_t size)
{
    switch (form)
    {
        case IdForm::Absent:
            return std::nullopt;
        case IdForm::Valid:
            return sequenceBytes(size);
        case IdForm::WrongSize:
            return sequenceBytes(size - 1);
        case IdForm::AllZero:
            return std::string(size, '\0');
    }
    return std::nullopt;
}

// One trace_flags input and the outcome the checks must give it.
struct FlagsForm
{
    std::string_view name;
    std::optional<std::uint32_t> sent;  // value set on the wire
    bool valid = false;                 // passes isValidTraceFlags
    std::optional<std::uint32_t> kept;  // value after sanitizing a valid context
};

constexpr std::array kFlagsForms{
    FlagsForm{.name = "absent", .sent = std::nullopt, .valid = true, .kept = std::nullopt},
    FlagsForm{.name = "sampled", .sent = kSampledBit, .valid = true, .kept = kSampledBit},
    FlagsForm{.name = "random", .sent = kRandomBit, .valid = true, .kept = kRandomBit},
    FlagsForm{.name = "undefined", .sent = kUndefinedBit, .valid = true, .kept = 0u},
    FlagsForm{.name = "max", .sent = kMaxTraceFlags, .valid = true, .kept = kKnownTraceFlags},
    FlagsForm{
        .name = "above max",
        .sent = kMaxTraceFlags + 1,
        .valid = false,
        .kept = std::nullopt},
};

// Builds a context from the given forms, sanitizes it and checks the result.
void
checkSanitized(IdForm traceId, IdForm spanId, FlagsForm const& flags)
{
    protocol::TraceContext tc;
    if (auto const bytes = idBytes(traceId, kTraceIdSize))
        tc.set_trace_id(*bytes);
    if (auto const bytes = idBytes(spanId, kSpanIdSize))
        tc.set_span_id(*bytes);
    if (flags.sent.has_value())
        tc.set_trace_flags(*flags.sent);

    bool const valid = traceId == IdForm::Valid && spanId == IdForm::Valid && flags.valid;
    EXPECT_EQ(isValidTraceContext(tc), valid);

    auto msg = validationWith(tc);
    sanitizeTraceContext(msg);

    ASSERT_EQ(msg.has_trace_context(), valid);
    if (!valid)
        return;
    EXPECT_EQ(msg.trace_context().trace_id(), tc.trace_id());
    EXPECT_EQ(msg.trace_context().span_id(), tc.span_id());
    EXPECT_EQ(flagsOf(msg.trace_context()), flags.kept);
}

}  // namespace

TEST(TraceContextValidation, trace_id_with_wrong_size_is_invalid)
{
    EXPECT_FALSE(isValidTraceId(sequenceBytes(kTraceIdSize - 1)));
    EXPECT_FALSE(isValidTraceId(sequenceBytes(kTraceIdSize + 1)));
    EXPECT_FALSE(isValidTraceId(std::string{}));
}

TEST(TraceContextValidation, trace_id_all_zero_is_invalid)
{
    EXPECT_FALSE(isValidTraceId(std::string(kTraceIdSize, '\0')));
}

TEST(TraceContextValidation, trace_id_with_one_non_zero_byte_is_valid)
{
    EXPECT_TRUE(isValidTraceId(lastByteSet(kTraceIdSize)));
}

TEST(TraceContextValidation, span_id_with_wrong_size_is_invalid)
{
    EXPECT_FALSE(isValidSpanId(sequenceBytes(kSpanIdSize - 1)));
    EXPECT_FALSE(isValidSpanId(sequenceBytes(kSpanIdSize + 1)));
    EXPECT_FALSE(isValidSpanId(std::string{}));
}

TEST(TraceContextValidation, span_id_all_zero_is_invalid)
{
    EXPECT_FALSE(isValidSpanId(std::string(kSpanIdSize, '\0')));
}

TEST(TraceContextValidation, span_id_with_one_non_zero_byte_is_valid)
{
    EXPECT_TRUE(isValidSpanId(lastByteSet(kSpanIdSize)));
}

TEST(TraceContextValidation, trace_flags_absent_is_valid)
{
    protocol::TraceContext const tc;
    EXPECT_TRUE(isValidTraceFlags(tc));
}

TEST(TraceContextValidation, trace_flags_at_max_is_valid)
{
    protocol::TraceContext tc;
    tc.set_trace_flags(kMaxTraceFlags);
    EXPECT_TRUE(isValidTraceFlags(tc));
}

TEST(TraceContextValidation, trace_flags_above_max_is_invalid)
{
    protocol::TraceContext tc;
    tc.set_trace_flags(kMaxTraceFlags + 1);
    EXPECT_FALSE(isValidTraceFlags(tc));
}

TEST(TraceContextValidation, context_with_valid_ids_and_no_flags_is_valid)
{
    EXPECT_TRUE(isValidTraceContext(validContext()));
}

TEST(TraceContextValidation, context_without_trace_id_is_invalid)
{
    auto tc = validContext();
    tc.clear_trace_id();
    EXPECT_FALSE(isValidTraceContext(tc));
}

TEST(TraceContextValidation, context_without_span_id_is_invalid)
{
    auto tc = validContext();
    tc.clear_span_id();
    EXPECT_FALSE(isValidTraceContext(tc));
}

TEST(TraceContextValidation, context_with_flags_above_max_is_invalid)
{
    auto tc = validContext();
    tc.set_trace_flags(kMaxTraceFlags + 1);
    EXPECT_FALSE(isValidTraceContext(tc));
}

TEST(TraceContextValidation, trace_flags_byte_absent_is_zero)
{
    protocol::TraceContext const tc;
    EXPECT_EQ(std::uint32_t{traceFlagsByte(tc)}, 0u);
}

TEST(TraceContextValidation, trace_flags_byte_keeps_sampled_bit)
{
    protocol::TraceContext tc;
    tc.set_trace_flags(kSampledBit);
    EXPECT_EQ(std::uint32_t{traceFlagsByte(tc)}, kSampledBit);
}

TEST(TraceContextValidation, trace_flags_byte_keeps_random_bit)
{
    protocol::TraceContext tc;
    tc.set_trace_flags(kRandomBit);
    EXPECT_EQ(std::uint32_t{traceFlagsByte(tc)}, kRandomBit);
}

TEST(TraceContextValidation, trace_flags_byte_clears_undefined_bit)
{
    protocol::TraceContext tc;
    tc.set_trace_flags(kUndefinedBit);
    EXPECT_EQ(std::uint32_t{traceFlagsByte(tc)}, 0u);
}

TEST(TraceContextValidation, trace_flags_byte_clears_unknown_bits)
{
    protocol::TraceContext tc;
    tc.set_trace_flags(kMaxTraceFlags);
    EXPECT_EQ(std::uint32_t{traceFlagsByte(tc)}, kKnownTraceFlags);
}

TEST(SanitizeTraceContext, absent_context_stays_absent)
{
    protocol::TMValidation msg;
    msg.set_validation(kBlob);
    auto const before = msg.SerializeAsString();

    sanitizeTraceContext(msg);

    EXPECT_FALSE(msg.has_trace_context());
    EXPECT_EQ(msg.SerializeAsString(), before);
}

TEST(SanitizeTraceContext, wrong_size_trace_id_clears_context)
{
    auto tc = validContext();
    tc.set_trace_id(sequenceBytes(kTraceIdSize - 1));
    auto msg = validationWith(tc);
    ASSERT_TRUE(msg.has_trace_context());

    sanitizeTraceContext(msg);

    EXPECT_FALSE(msg.has_trace_context());
    EXPECT_EQ(msg.validation(), kBlob);
}

TEST(SanitizeTraceContext, all_zero_span_id_clears_context)
{
    auto tc = validContext();
    tc.set_span_id(std::string(kSpanIdSize, '\0'));
    auto msg = validationWith(tc);
    ASSERT_TRUE(msg.has_trace_context());

    sanitizeTraceContext(msg);

    EXPECT_FALSE(msg.has_trace_context());
    EXPECT_EQ(msg.validation(), kBlob);
}

TEST(SanitizeTraceContext, flags_above_max_clear_context)
{
    auto tc = validContext();
    tc.set_trace_flags(kMaxTraceFlags + 1);
    auto msg = validationWith(tc);
    ASSERT_TRUE(msg.has_trace_context());

    sanitizeTraceContext(msg);

    EXPECT_FALSE(msg.has_trace_context());
    EXPECT_EQ(msg.validation(), kBlob);
}

TEST(SanitizeTraceContext, valid_context_keeps_ids_and_clears_unknown_flag_bits)
{
    auto tc = validContext();
    tc.set_trace_flags(kMaxTraceFlags);
    auto msg = validationWith(tc);

    sanitizeTraceContext(msg);

    ASSERT_TRUE(msg.has_trace_context());
    auto const& kept = msg.trace_context();
    EXPECT_EQ(kept.trace_id(), tc.trace_id());
    EXPECT_EQ(kept.span_id(), tc.span_id());
    EXPECT_EQ(flagsOf(kept), std::optional<std::uint32_t>{kKnownTraceFlags});
}

TEST(SanitizeTraceContext, undefined_flag_bit_is_cleared_and_context_kept)
{
    auto tc = validContext();
    tc.set_trace_flags(kUndefinedBit);
    auto msg = validationWith(tc);

    sanitizeTraceContext(msg);

    ASSERT_TRUE(msg.has_trace_context());
    auto const& kept = msg.trace_context();
    EXPECT_EQ(kept.trace_id(), tc.trace_id());
    EXPECT_EQ(kept.span_id(), tc.span_id());
    EXPECT_EQ(flagsOf(kept), std::optional<std::uint32_t>{0u});
}

TEST(SanitizeTraceContext, valid_context_without_flags_is_untouched)
{
    auto msg = validationWith(validContext());
    auto const before = msg.SerializeAsString();

    sanitizeTraceContext(msg);

    ASSERT_TRUE(msg.has_trace_context());
    EXPECT_FALSE(msg.trace_context().has_trace_flags());
    EXPECT_EQ(msg.SerializeAsString(), before);
}

TEST(SanitizeTraceContext, proposal_with_bad_context_is_cleared)
{
    protocol::TMProposeSet msg;
    msg.set_proposeseq(1);
    msg.set_currenttxhash(kBlob);
    msg.set_nodepubkey(kBlob);
    msg.set_closetime(1);
    msg.set_signature(kBlob);
    msg.set_previousledger(kBlob);
    auto tc = validContext();
    tc.set_trace_id(std::string(kTraceIdSize, '\0'));
    *msg.mutable_trace_context() = tc;
    ASSERT_TRUE(msg.IsInitialized());

    sanitizeTraceContext(msg);

    EXPECT_FALSE(msg.has_trace_context());
    EXPECT_TRUE(msg.IsInitialized());
}

TEST(SanitizeTraceContext, transaction_with_bad_context_is_cleared)
{
    auto tc = validContext();
    tc.set_span_id(sequenceBytes(kSpanIdSize + 1));
    auto msg = transactionWith(tc);
    ASSERT_TRUE(msg.IsInitialized());

    sanitizeTraceContext(msg);

    EXPECT_FALSE(msg.has_trace_context());
    EXPECT_TRUE(msg.IsInitialized());
}

// A valid span_id alone is not enough: after parsing, the transaction path
// gets a context only when both ids are valid.
TEST(SanitizeTraceContext, transaction_with_all_zero_trace_id_is_cleared)
{
    auto tc = validContext();
    tc.set_trace_id(std::string(kTraceIdSize, '\0'));
    ASSERT_TRUE(isValidSpanId(tc.span_id()));
    auto msg = transactionWith(tc);

    sanitizeTraceContext(msg);

    EXPECT_FALSE(msg.has_trace_context());
    EXPECT_TRUE(msg.IsInitialized());
}

// The invalid item is first and the valid one, with flags to clear, is
// last, so a loop that skips either end fails.
TEST(SanitizeTraceContext, batch_clears_only_invalid_items)
{
    auto bad = validContext();
    bad.set_span_id(std::string(kSpanIdSize, '\0'));
    auto good = validContext();
    good.set_trace_flags(kMaxTraceFlags);

    protocol::TMTransactions batch;
    *batch.add_transactions() = transactionWith(bad);
    *batch.add_transactions() = transactionWith(good);
    ASSERT_TRUE(batch.IsInitialized());

    sanitizeTraceContext(batch);

    ASSERT_EQ(batch.transactions_size(), 2);
    EXPECT_FALSE(batch.transactions(0).has_trace_context());
    ASSERT_TRUE(batch.transactions(1).has_trace_context());
    auto const& kept = batch.transactions(1).trace_context();
    EXPECT_EQ(kept.trace_id(), good.trace_id());
    EXPECT_EQ(kept.span_id(), good.span_id());
    EXPECT_EQ(flagsOf(kept), std::optional<std::uint32_t>{kKnownTraceFlags});
}

// Every combination of id and flag forms: a context is kept exactly when
// isValidTraceContext accepts it, and then only its unknown flag bits change.
TEST(SanitizeTraceContext, every_field_combination)
{
    for (auto const traceId : kIdForms)
    {
        for (auto const spanId : kIdForms)
        {
            for (auto const& flags : kFlagsForms)
            {
                SCOPED_TRACE(
                    ::testing::Message()
                    << "trace_id form " << std::to_underlying(traceId) << ", span_id form "
                    << std::to_underlying(spanId) << ", flags " << flags.name);
                checkSanitized(traceId, spanId, flags);
            }
        }
    }
}

// Field 4 of TraceContext is reserved, so a field 4 sent by a peer is an
// unknown field and is dropped on parse. A declared field 4 would survive.
TEST(TraceContextWire, reserved_field_4_is_dropped_on_parse)
{
    auto tc = validContext();
    tc.set_trace_flags(kSampledBit);
    auto const clean = validationWith(tc);

    auto withReserved = clean;
    withReserved.mutable_trace_context()->mutable_unknown_fields()->AddLengthDelimited(
        kReservedTraceStateField, "state");
    ASSERT_NE(withReserved.SerializeAsString(), clean.SerializeAsString());

    protocol::TMValidation parsed;
    ASSERT_TRUE(parsed.ParseFromString(withReserved.SerializeAsString()));
    // With field 4 declared, it parses as a known field and this count is 0.
    ASSERT_EQ(parsed.trace_context().unknown_fields().field_count(), 1);
    parsed.DiscardUnknownFields();

    EXPECT_EQ(parsed.SerializeAsString(), clean.SerializeAsString());
}

TEST(TraceContextWire, trace_state_field_is_reserved)
{
    auto const* const descriptor = protocol::TraceContext::descriptor();
    ASSERT_NE(descriptor, nullptr);

    EXPECT_EQ(descriptor->FindFieldByNumber(kReservedTraceStateField), nullptr);
    EXPECT_TRUE(descriptor->IsReservedNumber(kReservedTraceStateField));
    EXPECT_TRUE(descriptor->IsReservedName(kReservedTraceStateName));
}
