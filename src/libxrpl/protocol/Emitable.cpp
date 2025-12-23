#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Emitable.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

Emitable::Emitable()
{
    emitableTx_ = {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, delegatable, amendment, permissions, emitable, fields) \
    {value, emitable},
#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
    };

    granularEmitableMap_ = {
#pragma push_macro("EMITABLE")
#undef EMITABLE

#define EMITABLE(type, txType, value) {#type, type},

#include <xrpl/protocol/detail/emitable.macro>

#undef EMITABLE
#pragma pop_macro("EMITABLE")
    };

    granularNameMap_ = {
#pragma push_macro("EMITABLE")
#undef EMITABLE

#define EMITABLE(type, txType, value) {type, #type},

#include <xrpl/protocol/detail/emitable.macro>

#undef EMITABLE
#pragma pop_macro("EMITABLE")
    };

    granularTxTypeMap_ = {
#pragma push_macro("EMITABLE")
#undef EMITABLE

#define EMITABLE(type, txType, value) {type, txType},

#include <xrpl/protocol/detail/emitable.macro>

#undef EMITABLE
#pragma pop_macro("EMITABLE")
    };

    for ([[maybe_unused]] auto const& emitable : granularEmitableMap_)
        XRPL_ASSERT(
            emitable.second > UINT16_MAX,
            "xrpl::Emitable::granularEmitableMap_ : granular emitable "
            "value must not exceed the maximum uint16_t value.");
}

Emitable const&
Emitable::getInstance()
{
    static Emitable const instance;
    return instance;
}

std::optional<std::string>
Emitable::getEmitableName(std::uint32_t const value) const
{
    auto const emitableValue = static_cast<GranularEmitableType>(value);
    if (auto const granular = getGranularName(emitableValue))
        return *granular;

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
Emitable::getGranularName(GranularEmitableType const& value) const
{
    auto const it = granularNameMap_.find(value);
    if (it != granularNameMap_.end())
        return it->second;

    return std::nullopt;
}

std::optional<TxType>
Emitable::getGranularTxType(GranularEmitableType const& gpType) const
{
    auto const it = granularTxTypeMap_.find(gpType);
    if (it != granularTxTypeMap_.end())
        return it->second;

    return std::nullopt;
}

bool
Emitable::isEmitable(std::uint32_t const& emitableValue) const
{
    auto const granularEmitable = getGranularName(static_cast<GranularEmitableType>(emitableValue));
    if (granularEmitable)
        // granular emitables are always allowed to be delegated
        return true;

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

    if (it != emitableTx_.end() && it->second == Emittance::notEmitable)
        return false;

    return true;
}

uint32_t
Emitable::txToEmitableType(TxType const& type) const
{
    return static_cast<uint32_t>(type) + 1;
}

TxType
Emitable::emitableToTxType(uint32_t const& value) const
{
    return static_cast<TxType>(value - 1);
}

}  // namespace xrpl
