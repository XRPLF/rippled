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

// Forward declaration
class AccountSetBuilder;

/**
 * Transaction: AccountSet
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
     * Construct a AccountSet transaction wrapper from an existing STTx object.
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
     * Get sfEmailHash (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasEmailHash() const
    {
        return this->tx_->isFieldPresent(sfEmailHash);
    }

    /**
     * Get sfWalletLocator (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasWalletLocator() const
    {
        return this->tx_->isFieldPresent(sfWalletLocator);
    }

    /**
     * Get sfWalletSize (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasWalletSize() const
    {
        return this->tx_->isFieldPresent(sfWalletSize);
    }

    /**
     * Get sfMessageKey (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasMessageKey() const
    {
        return this->tx_->isFieldPresent(sfMessageKey);
    }

    /**
     * Get sfDomain (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasDomain() const
    {
        return this->tx_->isFieldPresent(sfDomain);
    }

    /**
     * Get sfTransferRate (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasTransferRate() const
    {
        return this->tx_->isFieldPresent(sfTransferRate);
    }

    /**
     * Get sfSetFlag (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasSetFlag() const
    {
        return this->tx_->isFieldPresent(sfSetFlag);
    }

    /**
     * Get sfClearFlag (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasClearFlag() const
    {
        return this->tx_->isFieldPresent(sfClearFlag);
    }

    /**
     * Get sfTickSize (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasTickSize() const
    {
        return this->tx_->isFieldPresent(sfTickSize);
    }

    /**
     * Get sfNFTokenMinter (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasNFTokenMinter() const
    {
        return this->tx_->isFieldPresent(sfNFTokenMinter);
    }
};

/**
 * Builder for AccountSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AccountSetBuilder : public TransactionBuilderBase<AccountSetBuilder>
{
public:
    AccountSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AccountSetBuilder>(ttACCOUNT_SET, account, sequence, fee)
    {
    }

    AccountSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttACCOUNT_SET)
        {
            throw std::runtime_error("Invalid transaction type for AccountSetBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfEmailHash (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setEmailHash(std::decay_t<typename SF_UINT128::type::value_type> const& value)
    {
        object_[sfEmailHash] = value;
        return *this;
    }

    /**
     * Set sfWalletLocator (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setWalletLocator(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfWalletLocator] = value;
        return *this;
    }

    /**
     * Set sfWalletSize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setWalletSize(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfWalletSize] = value;
        return *this;
    }

    /**
     * Set sfMessageKey (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setMessageKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMessageKey] = value;
        return *this;
    }

    /**
     * Set sfDomain (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setDomain(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfDomain] = value;
        return *this;
    }

    /**
     * Set sfTransferRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setTransferRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTransferRate] = value;
        return *this;
    }

    /**
     * Set sfSetFlag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setSetFlag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSetFlag] = value;
        return *this;
    }

    /**
     * Set sfClearFlag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setClearFlag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfClearFlag] = value;
        return *this;
    }

    /**
     * Set sfTickSize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setTickSize(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfTickSize] = value;
        return *this;
    }

    /**
     * Set sfNFTokenMinter (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountSetBuilder&
    setNFTokenMinter(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfNFTokenMinter] = value;
        return *this;
    }

    /**
     * Build and return the AccountSet wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
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
