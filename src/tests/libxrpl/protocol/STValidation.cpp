#include <xrpl/protocol/STValidation.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace xrpl;

namespace {

// Sign a validation with a throwaway key, then read it back without checking the
// signature, so isValid() has not run yet and runs on the first call.
std::shared_ptr<STValidation>
makeRoundTrippedValidation()
{
    auto const [publicKey, secretKey] = randomKeyPair(KeyType::Secp256k1);

    STValidation const original(
        NetClock::time_point{NetClock::duration{1000}},
        publicKey,
        secretKey,
        calcNodeID(publicKey),
        [](STValidation& val) {
            val.setFieldH256(sfLedgerHash, uint256{1});
            val.setFieldU32(sfLedgerSequence, 42);
        });

    Blob const serialized = original.getSerialized();
    SerialIter sit(makeSlice(serialized));

    return std::make_shared<STValidation>(
        std::ref(sit),
        [](PublicKey const& pk) { return calcNodeID(pk); },
        STValidation::DeserializeOptions{.checkSignature = false, .requireCanonicalOrder = false});
}

}  // namespace

// A validation whose signature we produced ourselves must verify. Without this
// the negative test below could pass for the wrong reason.
TEST(STValidation, is_valid_accepts_a_correctly_signed_validation)
{
    auto const val = makeRoundTrippedValidation();
    EXPECT_TRUE(val->isValid());
}

// isValid() checks the signature by re-serializing the signed fields, which can
// throw on a field large enough that its length has no encoding. isValid() is
// noexcept, so it must report the validation as invalid rather than propagate.
// The failure is not remembered, so the same object verifies again once it can
// be serialized.
TEST(STValidation, is_valid_absorbs_a_serialization_failure_without_remembering_it)
{
    auto const val = makeRoundTrippedValidation();

    // One entry past the largest field length the serializer can write a header
    // for. Every entry is 32 bytes, so round up to a whole entry.
    std::size_t const numEntries =
        (static_cast<std::size_t>(Serializer::kMaxValueOfLengthFor3ByteHeader) / uint256::size()) +
        1;
    ASSERT_GT(
        numEntries * uint256::size(),
        static_cast<std::size_t>(Serializer::kMaxValueOfLengthFor3ByteHeader));

    std::vector<uint256> entries(numEntries, uint256{1});
    val->setFieldV256(sfAmendments, STVector256{std::move(entries)});

    // Serializing really does fail, so the test below exercises the catch and
    // not some other path. Only the throw matters here, so the hash is
    // discarded explicitly.
    EXPECT_THROW(static_cast<void>(val->getSigningHash()), std::overflow_error);

    EXPECT_FALSE(val->isValid());

    // sfAmendments was never part of the signed object, so removing it leaves
    // exactly the fields the signature covers. The second call must therefore
    // check the signature again and accept it. Had the first call remembered
    // its failure, it would skip the check and keep reporting invalid.
    val->makeFieldAbsent(sfAmendments);
    EXPECT_TRUE(val->isValid());
}
