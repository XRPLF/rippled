#include <xrpl/protocol/TxFormats.h>

#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>  // IWYU pragma: keep
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/jss.h>  // IWYU pragma: keep

#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <vector>

namespace xrpl {

std::vector<SOElement> const&
TxFormats::getCommonFields()
{
    static auto const kCommonFields = std::vector<SOElement>{
        {sfTransactionType, SoeRequired},
        {sfFlags, SoeOptional},
        {sfSourceTag, SoeOptional},
        {sfAccount, SoeRequired},
        {sfSequence, SoeRequired},
        {sfPreviousTxnID, SoeOptional},  // emulate027
        {sfLastLedgerSequence, SoeOptional},
        {sfAccountTxnID, SoeOptional},
        {sfFee, SoeRequired},
        {sfOperationLimit, SoeOptional},
        {sfMemos, SoeOptional},
        {sfSigningPubKey, SoeRequired},
        {sfTicketSequence, SoeOptional},
        {sfTxnSignature, SoeOptional},
        {sfSigners, SoeOptional},  // submit_multisigned
        {sfNetworkID, SoeOptional},
        {sfDelegate, SoeOptional},
        {sfSponsor, SoeOptional},
        {sfSponsorFlags, SoeOptional},
        {sfSponsorSignature, SoeOptional},
    };
    return kCommonFields;
}

TxFormats::TxFormats()
{
#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define UNWRAP(...) __VA_ARGS__
#define TRANSACTION(tag, value, name, settings, fields) \
    add(jss::name, tag, UNWRAP fields, getCommonFields());

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
}

TxFormats const&
TxFormats::getInstance()
{
    static TxFormats const kInstance;
    return kInstance;
}

std::expected<TxType, TxTypeJsonError>
txTypeFromJson(json::Value const& value)
{
    constexpr auto kMaxType = std::numeric_limits<std::uint16_t>::max();

    if (value.isString())
    {
        std::string const text = value.asString();
        if (!text.empty() && (text[0] < '0' || text[0] > '9'))
        {
            auto const* const format = TxFormats::getInstance().findByName(text);
            if (format == nullptr)
                return std::unexpected(TxTypeJsonError::InvalidData);
            return format->getType();
        }

        std::uint16_t number = 0;
        if (!beast::lexicalCastChecked(number, text))
            return std::unexpected(TxTypeJsonError::InvalidData);
        return safeCast<TxType>(number);
    }

    if (value.isInt())
    {
        auto const number = value.asInt();
        if (number < 0 || number > kMaxType)
            return std::unexpected(TxTypeJsonError::InvalidData);
        return safeCast<TxType>(unsafeCast<std::uint16_t>(number));
    }

    if (value.isUInt())
    {
        auto const number = value.asUInt();
        if (number > kMaxType)
            return std::unexpected(TxTypeJsonError::InvalidData);
        return safeCast<TxType>(unsafeCast<std::uint16_t>(number));
    }

    return std::unexpected(TxTypeJsonError::BadType);
}

}  // namespace xrpl
