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

class PermissionedDomainDeleteBuilder;

/**
 * @brief Transaction: PermissionedDomainDelete
 *
 * Type: ttPERMISSIONED_DOMAIN_DELETE (63)
 * Delegable: Delegation::delegable
 * Amendment: featurePermissionedDomains
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PermissionedDomainDeleteBuilder to construct new transactions.
 */
class PermissionedDomainDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERMISSIONED_DOMAIN_DELETE;

    /**
     * @brief Construct a PermissionedDomainDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PermissionedDomainDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PermissionedDomainDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfDomainID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getDomainID() const
    {
        return this->tx_->at(sfDomainID);
    }
};

/**
 * @brief Builder for PermissionedDomainDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PermissionedDomainDeleteBuilder : public TransactionBuilderBase<PermissionedDomainDeleteBuilder>
{
public:
    /**
     * @brief Construct a new PermissionedDomainDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param domainID The sfDomainID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PermissionedDomainDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& domainID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PermissionedDomainDeleteBuilder>(ttPERMISSIONED_DOMAIN_DELETE, account, sequence, fee)
    {
        setDomainID(domainID);
    }

    /**
     * @brief Construct a PermissionedDomainDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PermissionedDomainDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERMISSIONED_DOMAIN_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for PermissionedDomainDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfDomainID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainDeleteBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Build and return the PermissionedDomainDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PermissionedDomainDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PermissionedDomainDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
