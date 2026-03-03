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
class XChainAccountCreateCommitBuilder;

/**
 * Transaction: XChainAccountCreateCommit
 * Type: ttXCHAIN_ACCOUNT_CREATE_COMMIT (44)
 * Delegable: Delegation::delegable
 * Amendment: featureXChainBridge
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainAccountCreateCommitBuilder to construct new transactions.
 */
class XChainAccountCreateCommit : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_ACCOUNT_CREATE_COMMIT;

    /**
     * Construct a XChainAccountCreateCommit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainAccountCreateCommit(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainAccountCreateCommit");
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
     * Get sfDestination (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_.at(sfDestination);
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
     * Get sfSignatureReward (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSignatureReward() const
    {
        return this->tx_.at(sfSignatureReward);
    }
};

/**
 * Builder for XChainAccountCreateCommit transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainAccountCreateCommitBuilder : public TransactionBuilderBase<XChainAccountCreateCommitBuilder>
{
public:
    XChainAccountCreateCommitBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& signatureReward,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainAccountCreateCommitBuilder>(ttXCHAIN_ACCOUNT_CREATE_COMMIT, account, sequence, fee)
    {
        setXChainBridge(xChainBridge);
        setDestination(destination);
        setAmount(amount);
        setSignatureReward(signatureReward);
    }

    XChainAccountCreateCommitBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttXCHAIN_ACCOUNT_CREATE_COMMIT)
        {
            throw std::runtime_error("Invalid transaction type for XChainAccountCreateCommitBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAccountCreateCommitBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAccountCreateCommitBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAccountCreateCommitBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAccountCreateCommitBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * Build and return the completed XChainAccountCreateCommit wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    XChainAccountCreateCommit
    build()
    {
        return XChainAccountCreateCommit(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
