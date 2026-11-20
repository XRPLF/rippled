#include <xrpl/protocol/Serializer.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STVector256.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace xrpl;

TEST(Serializer, add32_roundtrip)
{
    static constexpr auto kValues = std::to_array<std::int32_t>({
        std::numeric_limits<std::int32_t>::min(),
        -1,
        0,
        1,
        std::numeric_limits<std::int32_t>::max(),
    });

    for (std::int32_t const value : kValues)
    {
        Serializer s;
        s.add32(value);
        EXPECT_EQ(s.size(), 4);
        SerialIter sit(s.slice());
        EXPECT_EQ(sit.geti32(), value);
    }
}

TEST(Serializer, add64_roundtrip)
{
    static constexpr auto kValues = std::to_array<std::int64_t>({
        std::numeric_limits<std::int64_t>::min(),
        -1,
        0,
        1,
        std::numeric_limits<std::int64_t>::max(),
    });

    for (std::int64_t const value : kValues)
    {
        Serializer s;
        s.add64(value);
        EXPECT_EQ(s.size(), 8);
        SerialIter sit(s.slice());
        EXPECT_EQ(sit.geti64(), value);
    }
}

namespace {

// The decoder takes header bytes, while the constants describing their ranges
// are ints. Narrow one for a call.
constexpr std::byte
asByte(int value)
{
    return static_cast<std::byte>(value);
}

// Largest value a single header byte can hold.
constexpr int kMaxValueOfOneByte = Serializer::kNumberOfValuesInOneByte - 1;

// Build the three bytes a 3 byte header uses to state the given length. This is
// the inverse of the decoder's arithmetic, so the tests check it against a
// length the encoder accepts before relying on it for one the encoder rejects.
std::array<std::byte, Serializer::kMaxNumberOfBytesInHeader>
makeVLHeader3(int length)
{
    int const offset = length - Serializer::kMinValueOfLengthFor3ByteHeader;
    return {
        asByte(
            Serializer::kMinValueOfFirstByteFor3ByteHeader +
            (offset / Serializer::kNumberOfValuesInTwoBytes)),
        asByte(
            (offset / Serializer::kNumberOfValuesInOneByte) % Serializer::kNumberOfValuesInOneByte),
        asByte(offset % Serializer::kNumberOfValuesInOneByte),
    };
}

Blob
toBlob(std::array<std::byte, Serializer::kMaxNumberOfBytesInHeader> const& header)
{
    Blob bytes;
    for (std::byte const value : header)
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

// Read a length header, supplied as raw bytes, the way the deserializer does.
int
readVLHeader(std::array<std::byte, Serializer::kMaxNumberOfBytesInHeader> const& header)
{
    Blob const bytes = toBlob(header);
    SerialIter sit(bytes.data(), bytes.size());
    return sit.getVLDataLength();
}

// Check that a call fails with exactly this message. Every check in the
// serializer throws the same exception type, so the type alone does not say
// which check rejected the input.
template <class Callable>
testing::AssertionResult
throwsOverflow(Callable&& call, std::string_view expectedMessage)
{
    try
    {
        call();
    }
    catch (std::overflow_error const& error)
    {
        if (expectedMessage == error.what())
            return testing::AssertionSuccess();

        return testing::AssertionFailure()
            << "threw \"" << error.what() << "\", expected \"" << expectedMessage << '"';
    }
    catch (std::exception const& error)
    {
        return testing::AssertionFailure() << "threw the wrong type: " << error.what();
    }

    return testing::AssertionFailure() << "did not throw";
}

}  // namespace

// A length the encoder can write must still decode to itself. These are the
// first and last length each header size covers, so they pin its boundaries.
TEST(Serializer, decode_vl_length_accepts_encodable_lengths)
{
    EXPECT_EQ(
        Serializer::decodeVLLength(asByte(Serializer::kMinValueOfLengthFor1ByteHeader)),
        Serializer::kMinValueOfLengthFor1ByteHeader);

    EXPECT_EQ(
        Serializer::decodeVLLength(asByte(Serializer::kMaxValueOfLengthFor1ByteHeader)),
        Serializer::kMaxValueOfLengthFor1ByteHeader);

    EXPECT_EQ(
        Serializer::decodeVLLength(
            asByte(Serializer::kMinValueOfFirstByteFor2ByteHeader), asByte(0)),
        Serializer::kMinValueOfLengthFor2ByteHeader);

    EXPECT_EQ(
        Serializer::decodeVLLength(
            asByte(Serializer::kMaxValueOfFirstByteFor2ByteHeader), asByte(kMaxValueOfOneByte)),
        Serializer::kMaxValueOfLengthFor2ByteHeader);

    auto const maxHeader = makeVLHeader3(Serializer::kMaxValueOfLengthFor3ByteHeader);
    EXPECT_EQ(
        Serializer::decodeVLLength(maxHeader[0], maxHeader[1], maxHeader[2]),
        Serializer::kMaxValueOfLengthFor3ByteHeader);

    // Smallest length that needs a 3 byte header.
    auto const minHeader = makeVLHeader3(Serializer::kMinValueOfLengthFor3ByteHeader);
    EXPECT_EQ(
        Serializer::decodeVLLength(minHeader[0], minHeader[1], minHeader[2]),
        Serializer::kMinValueOfLengthFor3ByteHeader);
}

// A 3 byte header can state lengths above the largest the encoder can write.
// Decoding must reject them rather than return a length that could never be
// serialized again.
TEST(Serializer, decode_vl_length_rejects_lengths_the_encoder_cannot_write)
{
    auto const firstBad = makeVLHeader3(Serializer::kMaxValueOfLengthFor3ByteHeader + 1);
    EXPECT_TRUE(throwsOverflow(
        [&firstBad] {
            static_cast<void>(Serializer::decodeVLLength(firstBad[0], firstBad[1], firstBad[2]));
        },
        "decodeVLLength 3 byte: length is too large to re-encode"));

    // Largest length the 3 byte arithmetic can express at all, which is
    // kMaxRepresentableLength.
    EXPECT_TRUE(throwsOverflow(
        [] {
            static_cast<void>(Serializer::decodeVLLength(
                asByte(Serializer::kMaxValueOfFirstByteFor3ByteHeader),
                asByte(kMaxValueOfOneByte),
                asByte(kMaxValueOfOneByte)));
        },
        "decodeVLLength 3 byte: length is too large to re-encode"));
}

// A first byte that means a longer header is not a 1 byte length, whichever
// longer header it means.
TEST(Serializer, decode_vl_length_rejects_a_first_byte_that_is_not_a_1byte_length)
{
    static constexpr std::string_view kExpected =
        "decodeVLLength 1 byte: first byte is not a length";

    for (int const firstByte : {
             Serializer::kMinValueOfFirstByteFor2ByteHeader,
             Serializer::kMaxValueOfFirstByteFor2ByteHeader,
             Serializer::kMinValueOfFirstByteFor3ByteHeader,
             Serializer::kMaxValueOfFirstByteFor3ByteHeader,
             kMaxValueOfOneByte,
         })
    {
        EXPECT_TRUE(throwsOverflow(
            [firstByte] { static_cast<void>(Serializer::decodeVLLength(asByte(firstByte))); },
            kExpected))
            << "first byte " << firstByte;
    }
}

// decodeLengthLength picks which overload above to call, so every first byte
// must map to a width, except the single value no header begins with.
TEST(Serializer, decode_length_length_maps_every_first_byte_to_a_width)
{
    EXPECT_EQ(
        Serializer::decodeLengthLength(asByte(Serializer::kMinValueOfFirstByteFor1ByteHeader)), 1);
    EXPECT_EQ(
        Serializer::decodeLengthLength(asByte(Serializer::kMaxValueOfFirstByteFor1ByteHeader)), 1);
    EXPECT_EQ(
        Serializer::decodeLengthLength(asByte(Serializer::kMinValueOfFirstByteFor2ByteHeader)), 2);
    EXPECT_EQ(
        Serializer::decodeLengthLength(asByte(Serializer::kMaxValueOfFirstByteFor2ByteHeader)), 2);
    EXPECT_EQ(
        Serializer::decodeLengthLength(asByte(Serializer::kMinValueOfFirstByteFor3ByteHeader)), 3);
    EXPECT_EQ(
        Serializer::decodeLengthLength(asByte(Serializer::kMaxValueOfFirstByteFor3ByteHeader)), 3);

    // One past the last first byte any header uses.
    EXPECT_TRUE(throwsOverflow(
        [] { static_cast<void>(Serializer::decodeLengthLength(asByte(kMaxValueOfOneByte))); },
        "decodeLengthLength: first byte does not start any header"));
}

// Each multi byte overload reads one width only, so a first byte belonging to a
// different width is refused rather than decoded against the wrong range.
TEST(Serializer, decode_vl_length_2_byte_rejects_a_first_byte_of_another_width)
{
    EXPECT_TRUE(throwsOverflow(
        [] {
            static_cast<void>(Serializer::decodeVLLength(
                asByte(Serializer::kMaxValueOfFirstByteFor1ByteHeader), asByte(0)));
        },
        "decodeVLLength 2 byte: first byte is below the range"));

    EXPECT_TRUE(throwsOverflow(
        [] {
            static_cast<void>(Serializer::decodeVLLength(
                asByte(Serializer::kMinValueOfFirstByteFor3ByteHeader), asByte(0)));
        },
        "decodeVLLength 2 byte: first byte is above the range"));
}

TEST(Serializer, decode_vl_length_3_byte_rejects_a_first_byte_of_another_width)
{
    EXPECT_TRUE(throwsOverflow(
        [] {
            static_cast<void>(Serializer::decodeVLLength(
                asByte(Serializer::kMaxValueOfFirstByteFor2ByteHeader), asByte(0), asByte(0)));
        },
        "decodeVLLength 3 byte: first byte is below the range"));

    // One past the last first byte a 3 byte header uses. The length guard would
    // also refuse this byte, so the message is what pins which check ran.
    EXPECT_TRUE(throwsOverflow(
        [] {
            static_cast<void>(
                Serializer::decodeVLLength(asByte(kMaxValueOfOneByte), asByte(0), asByte(0)));
        },
        "decodeVLLength 3 byte: first byte is above the range"));
}

// The same rejection must happen through the entry point the deserializer
// actually uses, before any payload is read.
TEST(Serializer, get_vl_data_length_rejects_lengths_the_encoder_cannot_write)
{
    auto const lastGood = makeVLHeader3(Serializer::kMaxValueOfLengthFor3ByteHeader);
    EXPECT_EQ(readVLHeader(lastGood), Serializer::kMaxValueOfLengthFor3ByteHeader);

    auto const firstBad = makeVLHeader3(Serializer::kMaxValueOfLengthFor3ByteHeader + 1);
    EXPECT_THROW(readVLHeader(firstBad), std::overflow_error);
}

// A field type reading its own length must be refused too, before it asks for
// any of the payload the length promises. STVector256 is the type of the
// sfAmendments field carried in a validation.
TEST(Serializer, field_decode_rejects_lengths_the_encoder_cannot_write)
{
    auto const firstBad = makeVLHeader3(Serializer::kMaxValueOfLengthFor3ByteHeader + 1);

    Blob const bytes = toBlob(firstBad);

    // Only the header is supplied. The length is checked before the payload is
    // read, so overflow_error means the length was refused; a runtime_error
    // would mean it was accepted and the read ran off the end instead.
    SerialIter sit(bytes.data(), bytes.size());
    EXPECT_THROW({ STVector256 const oversized(sit, sfAmendments); }, std::overflow_error);
}

// Every header size still round-trips through the public encode/decode pair.
TEST(Serializer, add_vl_roundtrip_at_header_size_boundaries)
{
    static constexpr auto kLengths = std::to_array<int>({
        0,
        1,
        Serializer::kMaxValueOfLengthFor1ByteHeader,
        Serializer::kMinValueOfLengthFor2ByteHeader,
        Serializer::kMaxValueOfLengthFor2ByteHeader,
        Serializer::kMinValueOfLengthFor3ByteHeader,
        Serializer::kMaxValueOfLengthFor3ByteHeader,
    });

    for (int const length : kLengths)
    {
        Blob const payload(static_cast<std::size_t>(length), 0xAB);

        Serializer s;
        s.addVL(payload);

        SerialIter sit(s.slice());
        EXPECT_EQ(sit.getVLDataLength(), length) << "length " << length;
    }
}

// A length the encoder cannot write must be refused at the point of writing,
// not silently truncated.
TEST(Serializer, add_vl_rejects_lengths_above_the_largest_header)
{
    Blob const payload(
        static_cast<std::size_t>(Serializer::kMaxValueOfLengthFor3ByteHeader) + 1, 0xAB);

    Serializer s;
    EXPECT_THROW(s.addVL(payload), std::overflow_error);
}

// addVL takes the length as a separate int, so a caller can pass a negative one.
// The 1 byte case accepts any length up to 192, and a negative number satisfies
// that too, so without a guard -1 would be written as a first byte of 255, which
// no header uses, and addVL would then go on to copy a negative number of bytes.
// A size too big for an int arrives here negative as well, which is why the
// message names both.
TEST(Serializer, add_vl_rejects_a_negative_length)
{
    Blob const payload(1, 0xAB);

    Serializer s;
    EXPECT_TRUE(throwsOverflow(
        [&s, &payload] { s.addVL(payload.data(), -1); },
        "addEncoded: length is negative or did not fit in an int"));

    // Refused before anything was written, so no partial header is left behind.
    EXPECT_EQ(s.size(), 0);
}
