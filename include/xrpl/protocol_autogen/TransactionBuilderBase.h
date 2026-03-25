#pragma once

#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::transactions {

/**
 * Base class for all transaction builders.
 * Provides common field setters that are available for all transaction types.
 */
template <typename Derived>
class TransactionBuilderBase
{
public:
    TransactionBuilderBase() = default;

    TransactionBuilderBase(
        SF_UINT16::type::value_type transactionType,
        SF_ACCOUNT::type::value_type account,
        std::optional<SF_UINT32::type::value_type> sequence,
        std::optional<SF_AMOUNT::type::value_type> fee)
    {
        // Don't call object_.set(soTemplate) - keep object_ as a free object.
        // This avoids creating STBase placeholders for soeDEFAULT fields,
        // which would cause applyTemplate() to throw "may not be explicitly
        // set to default" when building the STTx.
        // The STTx constructor will call applyTemplate() which properly
        // handles missing fields.
        object_[sfTransactionType] = transactionType;
        setAccount(account);

        if (sequence)
        {
            setSequence(*sequence);
        }
        if (fee)
        {
            setFee(*fee);
        }
    }

    /**
     * Set the account that is sending the transaction.
     * @param value Account address (typically as a string)
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setAccount(AccountID const& value)
    {
        object_[sfAccount] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the transaction fee.
     * @param value Fee in drops (typically as a string or number)
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setFee(STAmount const& value)
    {
        object_[sfFee] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the sequence number.
     * @param value Sequence number
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setSequence(std::uint32_t const& value)
    {
        object_[sfSequence] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the ticket sequence to use for this transaction.
     * When using a ticket, the regular sequence number is set to 0.
     * @param value Ticket sequence number
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setTicketSequence(std::uint32_t const& value)
    {
        object_[sfSequence] = 0u;
        object_[sfTicketSequence] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set transaction flags.
     * @param value Flags value
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setFlags(std::uint32_t const& value)
    {
        object_[sfFlags] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the source tag.
     * @param value Source tag
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setSourceTag(std::uint32_t const& value)
    {
        object_[sfSourceTag] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the last ledger sequence.
     * @param value Last ledger sequence number
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setLastLedgerSequence(std::uint32_t const& value)
    {
        object_[sfLastLedgerSequence] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the account transaction ID.
     * @param value Account transaction ID (typically as a hex string)
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setAccountTxnID(uint256 const& value)
    {
        object_[sfAccountTxnID] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the previous transaction ID.
     * Used for emulate027 compatibility.
     * @param value Previous transaction ID
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setPreviousTxnID(uint256 const& value)
    {
        object_[sfPreviousTxnID] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the operation limit.
     * @param value Operation limit
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setOperationLimit(std::uint32_t const& value)
    {
        object_[sfOperationLimit] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the memos array.
     * @param value Array of memo objects
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setMemos(STArray const& value)
    {
        object_.setFieldArray(sfMemos, value);
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the signers array for multi-signing.
     * @param value Array of signer objects
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setSigners(STArray const& value)
    {
        object_.setFieldArray(sfSigners, value);
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the network ID.
     * @param value Network ID
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setNetworkID(std::uint32_t const& value)
    {
        object_[sfNetworkID] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Set the delegate account for delegated transactions.
     * @param value Delegate account ID
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    setDelegate(AccountID const& value)
    {
        object_[sfDelegate] = value;
        return static_cast<Derived&>(*this);
    }

    /**
     * Get the underlying STObject.
     * @return The STObject
     */
    STObject const&
    getSTObject() const
    {
        return object_;
    }

protected:
    /**
     * Sign the transaction with the given keys.
     *
     * This sets the SigningPubKey field and computes the TxnSignature.
     *
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return Reference to the derived builder for method chaining.
     */
    Derived&
    sign(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        // Set the signing public key
        object_.setFieldVL(sfSigningPubKey, publicKey.slice());

        // Build the signing data: HashPrefix::txSign + serialized object
        // (without signing fields)
        Serializer s;
        s.add32(HashPrefix::txSign);
        object_.addWithoutSigningFields(s);

        // Sign and set the signature
        auto const sig = xrpl::sign(publicKey, secretKey, s.slice());
        object_.setFieldVL(sfTxnSignature, sig);

        return static_cast<Derived&>(*this);
    }

    STObject object_{sfTransaction};
};

}  // namespace xrpl::transactions
