#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>

#include <gtest/gtest.h>
#include <tx/wasm/fixtures/RealHostFixture.h>

#include <cstdint>

namespace xrpl::test {

struct CheckSignatureImpl : RealHostFixture
{
};

TEST_F(CheckSignatureImpl, valid_signature)
{
    auto const kp = generateKeyPair(KeyType::Secp256k1, randomSeed());
    auto const& pk = kp.first;
    auto const& sk = kp.second;
    auto const& message = std::string{"hello signature"};
    auto const sig = sign(pk, sk, Slice(message.data(), message.size()));

    auto const result = makeHost()->checkSignature(
        Slice{message.data(), message.size()}, Slice{sig.data(), sig.size()}, pk);
    expectValue(result, std::int32_t{1});
}

TEST_F(CheckSignatureImpl, invalid_signature)
{
    auto const kp = generateKeyPair(KeyType::Secp256k1, randomSeed());
    auto const& pk = kp.first;
    auto const& sk = kp.second;
    auto const& message = std::string{"hello signature"};
    auto const sig = sign(pk, sk, Slice(message.data(), message.size()));
    auto const badSignature = std::string(sig.size(), 0xFF);

    auto const result = makeHost()->checkSignature(
        Slice{message.data(), message.size()}, Slice{badSignature.data(), badSignature.size()}, pk);
    expectValue(result, std::int32_t{0});
}

TEST_F(CheckSignatureImpl, invalid_public_key)
{
    auto const kp = generateKeyPair(KeyType::Secp256k1, randomSeed());
    auto const kp2 = generateKeyPair(KeyType::Secp256k1, randomSeed());
    auto const& pk = kp.first;
    auto const& sk = kp.second;
    auto const& message = std::string{"hello signature"};
    auto const sig = sign(pk, sk, Slice(message.data(), message.size()));

    auto const result = makeHost()->checkSignature(
        Slice{message.data(), message.size()}, Slice{sig.data(), sig.size()}, kp2.first);
    expectValue(result, std::int32_t{0});
}

TEST_F(CheckSignatureImpl, empty_signature)
{
    auto const kp = generateKeyPair(KeyType::Secp256k1, randomSeed());
    auto const& pk = kp.first;
    auto const& message = std::string{"hello signature"};

    auto const result =
        makeHost()->checkSignature(Slice{message.data(), message.size()}, Slice{}, pk);
    expectValue(result, std::int32_t{0});
}

TEST_F(CheckSignatureImpl, empty_message)
{
    auto const kp = generateKeyPair(KeyType::Secp256k1, randomSeed());
    auto const& pk = kp.first;
    auto const& sk = kp.second;
    auto const& message = std::string{"hello signature"};
    auto const sig = sign(pk, sk, Slice(message.data(), message.size()));

    auto const result = makeHost()->checkSignature(Slice{}, Slice{sig.data(), sig.size()}, pk);
    expectValue(result, std::int32_t{0});
}

}  // namespace xrpl::test
