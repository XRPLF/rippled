// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class PermissionedDomainDeleteBuilder;

/**
 * Transaction: PermissionedDomainDelete
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
     * Construct a PermissionedDomainDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PermissionedDomainDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PermissionedDomainDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfDomainID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getDomainID() const
    {
        return this->tx_.at(sfDomainID);
    }
};

/**
 * Builder for PermissionedDomainDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PermissionedDomainDeleteBuilder : public TransactionBuilderBase<PermissionedDomainDeleteBuilder>
{
public:
    PermissionedDomainDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& domainID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PermissionedDomainDeleteBuilder>(ttPERMISSIONED_DOMAIN_DELETE, account, sequence, fee)
    {
        setDomainID(domainID);
    }

    PermissionedDomainDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttPERMISSIONED_DOMAIN_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for PermissionedDomainDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfDomainID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainDeleteBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Build and return the completed PermissionedDomainDelete wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, PermissionedDomainDelete>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, PermissionedDomainDelete>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
