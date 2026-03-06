#pragma once

#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/STObjectValidation.h>

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
        auto txType = static_cast<TxType>(transactionType);
        auto const& soTemplate = TxFormats::getInstance().findByType(txType)->getSOTemplate();
        object_.set(soTemplate);

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
     * @brief Validate the transaction
     * @return true if validation passes, false otherwise
     */
    [[nodiscard]]
    bool
    validate(std::string& reason) const
    {
        if (!object_.isFieldPresent(sfTransactionType))
        {
            return false;
        }

        auto transactionType = static_cast<TxType>(object_.getFieldU16(sfTransactionType));
        if (!protocol_autogen::validateSTObject(
                object_, TxFormats::getInstance().findByType(transactionType)->getSOTemplate()))
        {
            reason = "Transaction failed schema validation";
            return false;
        }

        // Pseudo transactions are not submitted to the network
        if (isPseudoTx(object_))
        {
            return true;
        }
        return passesLocalChecks(object_, reason);
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
