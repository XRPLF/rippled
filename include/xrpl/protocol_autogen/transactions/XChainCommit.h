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
class XChainCommitBuilder;

/**
 * Transaction: XChainCommit
 * Type: ttXCHAIN_COMMIT (42)
 * Delegable: Delegation::delegable
 * Amendment: featureXChainBridge
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainCommitBuilder to construct new transactions.
 */
class XChainCommit : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_COMMIT;

    /**
     * Construct a XChainCommit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainCommit(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainCommit");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfXChainBridge (soeREQUIRED)
     */
    [[nodiscard]]
    SF_XCHAIN_BRIDGE::type::value_type
    getXChainBridge() const
    {
        return this->tx_.at(sfXChainBridge);
    }

    /**
     * Get sfXChainClaimID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainClaimID() const
    {
        return this->tx_.at(sfXChainClaimID);
    }

    /**
     * Get sfAmount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_.at(sfAmount);
    }

    /**
     * Get sfOtherChainDestination (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOtherChainDestination() const
    {
        if (hasOtherChainDestination())
        {
            return this->tx_.at(sfOtherChainDestination);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOtherChainDestination() const
    {
        return this->tx_.isFieldPresent(sfOtherChainDestination);
    }
};

/**
 * Builder for XChainCommit transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainCommitBuilder : public TransactionBuilderBase<XChainCommitBuilder>
{
public:
    XChainCommitBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,                     std::decay_t<typename SF_UINT64::type::value_type> const& xChainClaimID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainCommitBuilder>(ttXCHAIN_COMMIT, account, sequence, fee)
    {
        setXChainBridge(xChainBridge);
        setXChainClaimID(xChainClaimID);
        setAmount(amount);
    }

    XChainCommitBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttXCHAIN_COMMIT)
        {
            throw std::runtime_error("Invalid transaction type for XChainCommitBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCommitBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfXChainClaimID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCommitBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCommitBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfOtherChainDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    XChainCommitBuilder&
    setOtherChainDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainDestination] = value;
        return *this;
    }

    /**
     * Build and return the completed XChainCommit wrapper.
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, XChainCommit>
    build()
    {
        return protocol_autogen::Owning<STTx, XChainCommit>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
