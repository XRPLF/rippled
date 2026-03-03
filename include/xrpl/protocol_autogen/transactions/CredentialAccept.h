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
class CredentialAcceptBuilder;

/**
 * Transaction: CredentialAccept
 * Type: ttCREDENTIAL_ACCEPT (59)
 * Delegable: Delegation::delegable
 * Amendment: featureCredentials
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CredentialAcceptBuilder to construct new transactions.
 */
class CredentialAccept : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCREDENTIAL_ACCEPT;

    /**
     * Construct a CredentialAccept transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CredentialAccept(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CredentialAccept");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfIssuer (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getIssuer() const
    {
        return this->tx_.at(sfIssuer);
    }

    /**
     * Get sfCredentialType (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getCredentialType() const
    {
        return this->tx_.at(sfCredentialType);
    }
};

/**
 * Builder for CredentialAccept transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CredentialAcceptBuilder : public TransactionBuilderBase<CredentialAcceptBuilder>
{
public:
    CredentialAcceptBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& issuer,                     std::decay_t<typename SF_VL::type::value_type> const& credentialType,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CredentialAcceptBuilder>(ttCREDENTIAL_ACCEPT, account, sequence, fee)
    {
        setIssuer(issuer);
        setCredentialType(credentialType);
    }

    CredentialAcceptBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttCREDENTIAL_ACCEPT)
        {
            throw std::runtime_error("Invalid transaction type for CredentialAcceptBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfIssuer (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialAcceptBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * Set sfCredentialType (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialAcceptBuilder&
    setCredentialType(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCredentialType] = value;
        return *this;
    }

    /**
     * Build and return the completed CredentialAccept wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    CredentialAccept
    build()
    {
        return CredentialAccept(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
