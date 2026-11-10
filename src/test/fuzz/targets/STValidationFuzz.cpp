#include <test/fuzz/FuzzCheck.h>
#include <test/fuzz/FuzzTargets.h>

#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>

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

}  // namespace xrpl::fuzz
