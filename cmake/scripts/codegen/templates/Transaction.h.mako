// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

class ${name}Builder;

/**
 * @brief Transaction: ${name}
 *
 * Type: ${tag} (${value})
 * Delegable: ${delegable}
 * Amendment: ${amendments}
 * Privileges: ${privileges}
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use ${name}Builder to construct new transactions.
 */
class ${name} : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ${tag};

    /**
     * @brief Construct a ${name} transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ${name}(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ${name}");
        }
    }

    // Transaction-specific field getters
% for field in fields:
% if field['typed']:

    /**
     * @brief Get ${field['name']} (${field['requirement']})
% if field.get('supports_mpt'):
     * @note This field supports MPT (Multi-Purpose Token) amounts.
% endif
% if field['requirement'] == 'soeREQUIRED':
     * @return The field value.
% else:
     * @return The field value, or std::nullopt if not present.
% endif
     */
% if field['requirement'] == 'soeREQUIRED':
    [[nodiscard]]
    ${field['typeData']['return_type']}
    get${field['name'][2:]}() const
    {
        return this->tx_->${field['typeData']['getter_method']}(${field['name']});
    }
% else:
    [[nodiscard]]
    ${field['typeData']['return_type_optional']}
    get${field['name'][2:]}() const
    {
        if (has${field['name'][2:]}())
        {
            return this->tx_->${field['typeData']['getter_method']}(${field['name']});
        }
        return std::nullopt;
    }

    /**
     * @brief Check if ${field['name']} is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    has${field['name'][2:]}() const
    {
        return this->tx_->isFieldPresent(${field['name']});
    }
% endif
% else:
    /**
     * @brief Get ${field['name']} (${field['requirement']})
% if field.get('supports_mpt'):
     * @note This field supports MPT (Multi-Purpose Token) amounts.
% endif
     * @note This is an untyped field.
% if field['requirement'] == 'soeREQUIRED':
     * @return The field value.
% else:
     * @return The field value, or std::nullopt if not present.
% endif
     */
% if field['requirement'] == 'soeREQUIRED':
    [[nodiscard]]
    ${field['typeData']['return_type']}
    get${field['name'][2:]}() const
    {
        return this->tx_->${field['typeData']['getter_method']}(${field['name']});
    }
% else:
    [[nodiscard]]
    ${field['typeData']['return_type_optional']}
    get${field['name'][2:]}() const
    {
        if (this->tx_->isFieldPresent(${field['name']}))
            return this->tx_->${field['typeData']['getter_method']}(${field['name']});
        return std::nullopt;
    }

    /**
     * @brief Check if ${field['name']} is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    has${field['name'][2:]}() const
    {
        return this->tx_->isFieldPresent(${field['name']});
    }
% endif
% endif
% endfor
};

<%
    required_fields = [f for f in fields if f['requirement'] == 'soeREQUIRED']
%>\
/**
 * @brief Builder for ${name} transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ${name}Builder : public TransactionBuilderBase<${name}Builder>
{
public:
    /**
     * @brief Construct a new ${name}Builder with required fields.
     * @param account The account initiating the transaction.
% for field in required_fields:
     * @param ${field['paramName']} The ${field['name']} field value.
% endfor
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    ${name}Builder(SF_ACCOUNT::type::value_type account,
% for i, field in enumerate(required_fields):
                     ${field['typeData']['setter_type']} ${field['paramName']},\
% endfor
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<${name}Builder>(${tag}, account, sequence, fee)
    {
% for field in required_fields:
        set${field['name'][2:]}(${field['paramName']});
% endfor
    }

    /**
     * @brief Construct a ${name}Builder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    ${name}Builder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ${tag})
        {
            throw std::runtime_error("Invalid transaction type for ${name}Builder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */
% for field in fields:

    /**
     * @brief Set ${field['name']} (${field['requirement']})
% if field.get('supports_mpt'):
     * @note This field supports MPT (Multi-Purpose Token) amounts.
% endif
     * @return Reference to this builder for method chaining.
     */
    ${name}Builder&
    set${field['name'][2:]}(${field['typeData']['setter_type']} value)
    {
% if field.get('stiSuffix') == 'ISSUE':
        object_[${field['name']}] = STIssue(${field['name']}, value);
% elif field['typeData'].get('setter_use_brackets'):
        object_[${field['name']}] = value;
% else:
        object_.${field['typeData']['setter_method']}(${field['name']}, value);
% endif
        return *this;
    }
% endfor

    /**
     * @brief Build and return the ${name} wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    ${name}
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return ${name}{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
