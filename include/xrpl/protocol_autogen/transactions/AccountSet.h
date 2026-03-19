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

class AccountSetBuilder;

/**
 * @brief Transaction: AccountSet
 *
 * Type: ttACCOUNT_SET (3)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AccountSetBuilder to construct new transactions.
 */
class AccountSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttACCOUNT_SET;

    /**
     * @brief Construct a AccountSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AccountSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AccountSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfEmailHash (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT128::type::value_type>
    getEmailHash() const
    {
        if (hasEmailHash())
        {
            return this->tx_->at(sfEmailHash);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfEmailHash is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasEmailHash() const
    {
        return this->tx_->isFieldPresent(sfEmailHash);
    }

    /**
     * @brief Get sfWalletLocator (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getWalletLocator() const
    {
        if (hasWalletLocator())
        {
            return this->tx_->at(sfWalletLocator);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfWalletLocator is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasWalletLocator() const
    {
        return this->tx_->isFieldPresent(sfWalletLocator);
    }

    /**
     * @brief Get sfWalletSize (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getWalletSize() const
    {
        if (hasWalletSize())
        {
            return this->tx_->at(sfWalletSize);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfWalletSize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasWalletSize() const
    {
        return this->tx_->isFieldPresent(sfWalletSize);
    }

    /**
     * @brief Get sfMessageKey (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMessageKey() const
    {
        if (hasMessageKey())
        {
            return this->tx_->at(sfMessageKey);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMessageKey is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMessageKey() const
    {
        return this->tx_->isFieldPresent(sfMessageKey);
    }

    /**
     * @brief Get sfDomain (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getDomain() const
    {
        if (hasDomain())
        {
            return this->tx_->at(sfDomain);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomain is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomain() const
    {
        return this->tx_->isFieldPresent(sfDomain);
    }

    /**
     * @brief Get sfTransferRate (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTransferRate() const
    {
        if (hasTransferRate())
        {
            return this->tx_->at(sfTransferRate);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfTransferRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTransferRate() const
    {
        return this->tx_->isFieldPresent(sfTransferRate);
    }

    /**
     * @brief Get sfSetFlag (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getSetFlag() const
    {
        if (hasSetFlag())
        {
            return this->tx_->at(sfSetFlag);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSetFlag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSetFlag() const
    {
        return this->tx_->isFieldPresent(sfSetFlag);
    }

    /**
     * @brief Get sfClearFlag (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getClearFlag() const
    {
        if (hasClearFlag())
        {
            return this->tx_->at(sfClearFlag);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfClearFlag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasClearFlag() const
    {
        return this->tx_->isFieldPresent(sfClearFlag);
    }

    /**
     * @brief Get sfTickSize (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getTickSize() const
    {
        if (hasTickSize())
        {
            return this->tx_->at(sfTickSize);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfTickSize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTickSize() const
    {
        return this->tx_->isFieldPresent(sfTickSize);
    }

    /**
     * @brief Get sfNFTokenMinter (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getNFTokenMinter() const
    {
        if (hasNFTokenMinter())
        {
            return this->tx_->at(sfNFTokenMinter);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfNFTokenMinter is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasNFTokenMinter() const
    {
        return this->tx_->isFieldPresent(sfNFTokenMinter);
    }
};

/**
 * @brief Builder for AccountSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AccountSetBuilder : public TransactionBuilderBase<AccountSetBuilder>
{
public:
    /**
     * @brief Construct a new AccountSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AccountSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AccountSetBuilder>(ttACCOUNT_SET, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a AccountSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AccountSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttACCOUNT_SET)
        {
            throw std::runtime_error("Invalid transaction type for AccountSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfEmailHash (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setEmailHash(std::decay_t<typename SF_UINT128::type::value_type> const& value)
    {
        object_[sfEmailHash] = value;
        return *this;
    }

    /**
     * @brief Set sfWalletLocator (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setWalletLocator(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfWalletLocator] = value;
        return *this;
    }

    /**
     * @brief Set sfWalletSize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setWalletSize(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfWalletSize] = value;
        return *this;
    }

    /**
     * @brief Set sfMessageKey (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setMessageKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMessageKey] = value;
        return *this;
    }

    /**
     * @brief Set sfDomain (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setDomain(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfDomain] = value;
        return *this;
    }

    /**
     * @brief Set sfTransferRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setTransferRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTransferRate] = value;
        return *this;
    }

    /**
     * @brief Set sfSetFlag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setSetFlag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSetFlag] = value;
        return *this;
    }

    /**
     * @brief Set sfClearFlag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setClearFlag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfClearFlag] = value;
        return *this;
    }

    /**
     * @brief Set sfTickSize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setTickSize(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfTickSize] = value;
        return *this;
    }

    /**
     * @brief Set sfNFTokenMinter (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setNFTokenMinter(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfNFTokenMinter] = value;
        return *this;
    }

    /**
     * @brief Build and return the AccountSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AccountSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AccountSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
