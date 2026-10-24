#include <xrpl/protocol/Emitable.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>  // IWYU pragma: keep
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxSettings.h>

#include <cstdint>
#include <optional>
#include <string>

namespace xrpl {

Emitable::Emitable()
{
    emitableTx_ = {
#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define UNWRAP(...) __VA_ARGS__
#define TRANSACTION(tag, value, name, settings, ...) \
    {value, (TxSettings UNWRAP settings).emittance},
#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
    };

    granularEmitableMap_ = {
#pragma push_macro("EMITABLE")
#undef EMITABLE

#define EMITABLE(type, txType, value) {#type, value},

#include <xrpl/protocol/detail/emitable.macro>

#undef EMITABLE
#pragma pop_macro("EMITABLE")
    };

    granularNameMap_ = {
#pragma push_macro("EMITABLE")
#undef EMITABLE

#define EMITABLE(type, txType, value) {value, #type},

#include <xrpl/protocol/detail/emitable.macro>

#undef EMITABLE
#pragma pop_macro("EMITABLE")
    };

    granularTxTypeMap_ = {
#pragma push_macro("EMITABLE")
#undef EMITABLE

#define EMITABLE(type, txType, value) {value, txType},

#include <xrpl/protocol/detail/emitable.macro>

#undef EMITABLE
#pragma pop_macro("EMITABLE")
    };

    for ([[maybe_unused]] auto const& emitable : granularEmitableMap_)
    {
        XRPL_ASSERT(
            emitable.second > UINT16_MAX,
            "xrpl::Emitable::granularEmitableMap_ : granular emitable "
            "value must not exceed the maximum uint16_t value.");
    }
}

Emitable const&
Emitable::getInstance()
{
    static Emitable const kInstance;
    return kInstance;
}

std::optional<std::string>
Emitable::getEmitableName(std::uint32_t const value) const
{
    if (auto const granular = getGranularName(value))
        return granular;

    // not a granular emitable, check if it maps to a transaction type
    auto const txType = emitableToTxType(value);
    if (auto const* item = TxFormats::getInstance().findByType(txType); item != nullptr)
        return item->getName();

    return std::nullopt;
}

std::optional<std::uint32_t>
Emitable::getGranularValue(std::string const& name) const
{
    auto const it = granularEmitableMap_.find(name);
    if (it != granularEmitableMap_.end())
        return static_cast<uint32_t>(it->second);

    return std::nullopt;
}

std::optional<std::string>
Emitable::getGranularName(std::uint32_t value) const
{
    auto const it = granularNameMap_.find(value);
    if (it != granularNameMap_.end())
        return it->second;

    return std::nullopt;
}

std::optional<TxType>
Emitable::getGranularTxType(std::uint32_t value) const
{
    auto const it = granularTxTypeMap_.find(value);
    if (it != granularTxTypeMap_.end())
        return it->second;

    return std::nullopt;
}

bool
Emitable::isEmitable(std::uint32_t const& emitableValue) const
{
    auto const granularEmitable = getGranularName(emitableValue);
    if (granularEmitable)
    {
        // granular emitables are always allowed to be delegated
        return true;
    }

    auto const txType = emitableToTxType(emitableValue);
    auto const it = emitableTx_.find(txType);

    // if (rules.enabled(fixDelegateV1_1))
    // {
    //     if (it == delegatableTx_.end())
    //         return false;

    //     auto const txFeaturesIt = txFeatureMap_.find(txType);
    //     XRPL_ASSERT(
    //         txFeaturesIt != txFeatureMap_.end(),
    //         "xrpl::Emitables::isDelegatable : tx exists in txFeatureMap_");

    //     // fixDelegateV1_1: Delegation is only allowed if the required
    //     amendment
    //     // for the transaction is enabled. For transactions that do not
    //     require
    //     // an amendment, delegation is always allowed.
    //     if (txFeaturesIt->second != uint256{} &&
    //         !rules.enabled(txFeaturesIt->second))
    //         return false;
    // }

    return it == emitableTx_.end() || it->second != Emittance::NotEmitable;
}

std::uint32_t
Emitable::txToEmitableType(TxType type)
{
    return static_cast<uint32_t>(type) + 1;
}

TxType
Emitable::emitableToTxType(std::uint32_t value)
{
    return static_cast<TxType>(value - 1);
}

}  // namespace xrpl
