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

// Forward declaration
class ${name}Builder;

/**
 * Transaction: ${name}
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
     * Construct a ${name} transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit ${name}(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for ${name}");
        }
    }

    // Transaction-specific field getters
% for field in fields:
% if field['typed']:

    /**
     * Get ${field['name']} (${field['requirement']})
% if field.get('supports_mpt'):
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
% endif
     */
% if field['requirement'] == 'soeREQUIRED':
    [[nodiscard]]
    ${field['typeData']['return_type']}
    get${field['name'][2:]}() const
    {
        return this->tx_.${field['typeData']['getter_method']}(${field['name']});
    }
% else:
    [[nodiscard]]
    ${field['typeData']['return_type_optional']}
    get${field['name'][2:]}() const
    {
        if (has${field['name'][2:]}())
        {
            return this->tx_.${field['typeData']['getter_method']}(${field['name']});
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    has${field['name'][2:]}() const
    {
        return this->tx_.isFieldPresent(${field['name']});
    }
% endif
% else:
    /**
     * Get ${field['name']} (${field['requirement']})
% if field.get('supports_mpt'):
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
% endif
     * Note: This is an untyped field
     */
% if field['requirement'] == 'soeREQUIRED':
    [[nodiscard]]
    ${field['typeData']['return_type']}
    get${field['name'][2:]}() const
    {
        return this->tx_.${field['typeData']['getter_method']}(${field['name']});
    }
% else:
    [[nodiscard]]
    ${field['typeData']['return_type_optional']}
    get${field['name'][2:]}() const
    {
        if (this->tx_.isFieldPresent(${field['name']}))
            return this->tx_.${field['typeData']['getter_method']}(${field['name']});
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    has${field['name'][2:]}() const
    {
        return this->tx_.isFieldPresent(${field['name']});
    }
% endif
% endif
% endfor
};

<%
    required_fields = [f for f in fields if f['requirement'] == 'soeREQUIRED']
%>\
/**
 * Builder for ${name} transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class ${name}Builder : public TransactionBuilderBase<${name}Builder>
{
public:
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

    ${name}Builder(STTx const& tx)
    {
        if (tx.getTxnType() != ${tag})
        {
            throw std::runtime_error("Invalid transaction type for ${name}Builder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters
% for field in fields:

    /**
     * Set ${field['name']} (${field['requirement']})
% if field.get('supports_mpt'):
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
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
     * Build and return the completed ${name} wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    ${name}
    build()
    {
        return ${name}(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
