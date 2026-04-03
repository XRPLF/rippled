// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::ledger_entries {

class ${name}Builder;

/**
 * @brief Ledger Entry: ${name}
 *
 * Type: ${tag} (${value})
 * RPC Name: ${rpc_name}
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use ${name}Builder to construct new ledger entries.
 */
class ${name} : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ${tag};

    /**
     * @brief Construct a ${name} ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit ${name}(std::shared_ptr<SLE const> sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for ${name}");
        }
    }

    // Ledger entry-specific field getters
% for field in fields:
% if field['typed']:

    /**
     * @brief Get ${field['name']} (${field['requirement']})
% if field.get('mpt_support'):
     * MPT Support: ${field['mpt_support']}
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
        return this->sle_->${field['typeData']['getter_method']}(${field['name']});
    }
% else:
    [[nodiscard]]
    ${field['typeData']['return_type_optional']}
    get${field['name'][2:]}() const
    {
        if (has${field['name'][2:]}())
            return this->sle_->${field['typeData']['getter_method']}(${field['name']});
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
        return this->sle_->isFieldPresent(${field['name']});
    }
% endif
% else:

    /**
     * @brief Get ${field['name']} (${field['requirement']})
% if field.get('mpt_support'):
     * MPT Support: ${field['mpt_support']}
% endif
     * @note This is an untyped field (${field.get('cppType', 'unknown')}).
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
        return this->sle_->${field['typeData']['getter_method']}(${field['name']});
    }
% else:
    [[nodiscard]]
    ${field['typeData']['return_type_optional']}
    get${field['name'][2:]}() const
    {
        if (this->sle_->isFieldPresent(${field['name']}))
            return this->sle_->${field['typeData']['getter_method']}(${field['name']});
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
        return this->sle_->isFieldPresent(${field['name']});
    }
% endif
% endif
% endfor
};

<%
    required_fields = [f for f in fields if f['requirement'] == 'soeREQUIRED']
%>\
/**
 * @brief Builder for ${name} ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses Json::Value internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class ${name}Builder : public LedgerEntryBuilderBase<${name}Builder>
{
public:
    /**
     * @brief Construct a new ${name}Builder with required fields.
% for field in required_fields:
     * @param ${field['paramName']} The ${field['name']} field value.
% endfor
     */
    ${name}Builder(\
% for i, field in enumerate(required_fields):
${field['typeData']['setter_type']} ${field['paramName']}${',' if i < len(required_fields) - 1 else ''}\
% endfor
)
        : LedgerEntryBuilderBase<${name}Builder>(${tag})
    {
% for field in required_fields:
        set${field['name'][2:]}(${field['paramName']});
% endfor
    }

    /**
     * @brief Construct a ${name}Builder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    ${name}Builder(std::shared_ptr<SLE const> sle)
    {
        if (sle->at(sfLedgerEntryType) != ${tag})
        {
            throw std::runtime_error("Invalid ledger entry type for ${name}");
        }
        object_ = *sle;
    }

    /** @brief Ledger entry-specific field setters */
% for field in fields:

    /**
     * @brief Set ${field['name']} (${field['requirement']})
% if field.get('mpt_support'):
     * MPT Support: ${field['mpt_support']}
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
     * @brief Build and return the completed ${name} wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    ${name}
    build(uint256 const& index)
    {
        return ${name}{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
