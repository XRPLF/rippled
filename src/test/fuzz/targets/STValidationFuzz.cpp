#include <test/fuzz/FuzzCheck.h>
#include <test/fuzz/FuzzTargets.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace xrpl::fuzz {

namespace {

std::shared_ptr<STValidation const>
parseValidation(SerialIter& sit, bool requireCanonicalOrder)
{
    return std::make_shared<STValidation const>(
        sit,
        [](PublicKey const& pk) { return calcNodeID(pk); },
        STValidation::DeserializeOptions{
            .checkSignature = false,
            .requireCanonicalOrder = requireCanonicalOrder,
        });
}

}  // namespace

/**
 * @brief Parse arbitrary bytes as a validation, the way PeerImp does for TMValidation.
 *
 * Signatures are not checked, so mutated inputs reach the field parsing and the
 * validation-specific accessors instead of failing at signature verification.
 */
int
fuzzSTValidation(std::uint8_t const* data, std::size_t size)
{
    static constexpr char const* kName = "st_validation";

    for (bool const canonical : {false, true})
    {
        auto const val = tryOrEmpty([&] {
            SerialIter sit{data, size};
            return parseValidation(sit, canonical);
        });
        if (!val)
            continue;

        // Re-serialization sorts fields, so its output must always pass the canonical-order check.
        auto const again = checkSerializationFixedPoint(
            *val, [](SerialIter& sit) { return parseValidation(sit, true); }, kName);
        check(again->getNodeID() == val->getNodeID(), kName, "re-parsed validation changed node");
        check(
            again->getLedgerHash() == val->getLedgerHash(),
            kName,
            "re-parsed validation changed ledger hash");

        (void)val->isFull();
        (void)val->getSignTime();
        (void)val->getJson(JsonOptions::Values::None);
    }
    return 0;
}

/**
 * @brief A signed partial validation, and a full one with the optional load fee and cookie.
 *
 * The key comes from a fixed passphrase, so the seeds are the same on every run.
 */
std::vector<Blob>
stValidationSeeds()
{
    auto const [pk, sk] = generateKeyPair(KeyType::Secp256k1, generateSeed("fuzz seeds"));

    std::vector<Blob> seeds;
    for (bool const full : {false, true})
    {
        STValidation const val(
            NetClock::time_point{NetClock::duration{1}},
            pk,
            sk,
            calcNodeID(pk),
            [&](STValidation& v) {
                v.setFieldH256(sfLedgerHash, UInt256{2});
                v.setFieldU32(sfLedgerSequence, 3);
                if (full)
                {
                    v.setFlag(kVfFullValidation);
                    v.setFieldU32(sfLoadFee, 256);
                    v.setFieldU64(sfCookie, 4);
                }
            });
        seeds.push_back(val.getSerializer().getData());
    }
    return seeds;
}

}  // namespace xrpl::fuzz
