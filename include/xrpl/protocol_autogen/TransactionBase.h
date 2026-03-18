#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol_autogen/STObjectValidation.h>
#include <xrpl/protocol_autogen/Utils.h>

#include <optional>
#include <string>

namespace xrpl::transactions {

/**
 * @brief Base class for all transaction wrapper types.
 *
 * Provides type-safe read-only accessors for common transaction fields.
 * This is an immutable wrapper around STTx. Use the corresponding Builder classes
 * to construct new transactions.
 */
class TransactionBase
{
public:
    /**
     * @brief Construct a transaction wrapper from an existing STTx object.
     * @param tx The underlying transaction object to wrap
     */
    explicit TransactionBase(std::shared_ptr<STTx const> tx) : tx_(std::move(tx))
    {
    }

    /**
     * @brief Validate the transaction
     * @return true if validation passes, false otherwise
     */
    [[nodiscard]]
    bool
    validate(std::string& reason) const
    {
        if (!protocol_autogen::validateSTObject(
                *tx_, TxFormats::getInstance().findByType(tx_->getTxnType())->getSOTemplate()))
        {
            // LCOV_EXCL_START
            reason = "Transaction failed schema validation";
            return false;
            // LCOV_EXCL_STOP
        }

        // Pseudo transactions are not submitted to the network
        if (isPseudoTx(*tx_))
        {
            return true;
        }
        return passesLocalChecks(*tx_, reason);
    }

    /**
     * @brief Get the transaction type.
     * @return The type of this transaction
     */
    [[nodiscard]]
    xrpl::TxType
    getTransactionType() const
    {
        return tx_->getTxnType();
    }

    /**
     * @brief Get the account initiating the transaction (sfAccount).
     *
     * This field is REQUIRED for all transactions.
     * @return The account ID of the transaction sender
     */
    [[nodiscard]]
    AccountID
    getAccount() const
    {
        return tx_->at(sfAccount);
    }

    /**
     * @brief Get the sequence number of the transaction (sfSequence).
     *
     * This field is REQUIRED for all transactions.
     * @return The sequence number
     */
    [[nodiscard]]
    std::uint32_t
    getSequence() const
    {
        return tx_->at(sfSequence);
    }

    /**
     * @brief Get the transaction fee (sfFee).
     *
     * This field is REQUIRED for all transactions.
     * @return The fee amount
     */
    [[nodiscard]]
    STAmount
    getFee() const
    {
        return tx_->at(sfFee);
    }

    /**
     * @brief Get the signing public key (sfSigningPubKey).
     *
     * This field is REQUIRED for all transactions.
     * @return The public key used for signing as a blob
     */
    [[nodiscard]]
    Blob
    getSigningPubKey() const
    {
        return tx_->getFieldVL(sfSigningPubKey);
    }

    /**
     * @brief Get the transaction flags (sfFlags).
     *
     * This field is OPTIONAL.
     * @return The flags value if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint32_t>
    getFlags() const
    {
        if (tx_->isFieldPresent(sfFlags))
            return tx_->at(sfFlags);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has flags set.
     * @return true if sfFlags is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasFlags() const
    {
        return tx_->isFieldPresent(sfFlags);
    }

    /**
     * @brief Get the source tag (sfSourceTag).
     *
     * This field is OPTIONAL and used to identify the source of a payment.
     * @return The source tag value if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint32_t>
    getSourceTag() const
    {
        if (tx_->isFieldPresent(sfSourceTag))
            return tx_->at(sfSourceTag);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has a source tag.
     * @return true if sfSourceTag is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasSourceTag() const
    {
        return tx_->isFieldPresent(sfSourceTag);
    }

    /**
     * @brief Get the previous transaction ID (sfPreviousTxnID).
     *
     * This field is OPTIONAL and used for transaction chaining.
     * @return The previous transaction ID if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint256>
    getPreviousTxnID() const
    {
        if (tx_->isFieldPresent(sfPreviousTxnID))
            return tx_->at(sfPreviousTxnID);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has a previous transaction ID.
     * @return true if sfPreviousTxnID is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasPreviousTxnID() const
    {
        return tx_->isFieldPresent(sfPreviousTxnID);
    }

    /**
     * @brief Get the last ledger sequence (sfLastLedgerSequence).
     *
     * This field is OPTIONAL and specifies the latest ledger sequence
     * in which this transaction can be included.
     * @return The last ledger sequence if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint32_t>
    getLastLedgerSequence() const
    {
        if (tx_->isFieldPresent(sfLastLedgerSequence))
            return tx_->at(sfLastLedgerSequence);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has a last ledger sequence.
     * @return true if sfLastLedgerSequence is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasLastLedgerSequence() const
    {
        return tx_->isFieldPresent(sfLastLedgerSequence);
    }

    /**
     * @brief Get the account transaction ID (sfAccountTxnID).
     *
     * This field is OPTIONAL and used to track transaction sequences.
     * @return The account transaction ID if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint256>
    getAccountTxnID() const
    {
        if (tx_->isFieldPresent(sfAccountTxnID))
            return tx_->at(sfAccountTxnID);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has an account transaction ID.
     * @return true if sfAccountTxnID is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasAccountTxnID() const
    {
        return tx_->isFieldPresent(sfAccountTxnID);
    }

    /**
     * @brief Get the operation limit (sfOperationLimit).
     *
     * This field is OPTIONAL and limits the number of operations in a transaction.
     * @return The operation limit if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint32_t>
    getOperationLimit() const
    {
        if (tx_->isFieldPresent(sfOperationLimit))
            return tx_->at(sfOperationLimit);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has an operation limit.
     * @return true if sfOperationLimit is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasOperationLimit() const
    {
        return tx_->isFieldPresent(sfOperationLimit);
    }

    /**
     * @brief Get the memos array (sfMemos).
     *
     * This field is OPTIONAL and contains arbitrary data attached to the transaction.
     * @note This is an untyped field (STArray).
     * @return A reference wrapper to the memos array if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getMemos() const
    {
        if (tx_->isFieldPresent(sfMemos))
            return tx_->getFieldArray(sfMemos);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has memos.
     * @return true if sfMemos is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasMemos() const
    {
        return tx_->isFieldPresent(sfMemos);
    }

    /**
     * @brief Get the ticket sequence (sfTicketSequence).
     *
     * This field is OPTIONAL and used when consuming a ticket instead of a sequence number.
     * @return The ticket sequence if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint32_t>
    getTicketSequence() const
    {
        if (tx_->isFieldPresent(sfTicketSequence))
            return tx_->at(sfTicketSequence);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has a ticket sequence.
     * @return true if sfTicketSequence is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasTicketSequence() const
    {
        return tx_->isFieldPresent(sfTicketSequence);
    }

    /**
     * @brief Get the transaction signature (sfTxnSignature).
     *
     * This field is OPTIONAL and contains the signature for single-signed transactions.
     * @return The transaction signature as a blob if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<Blob>
    getTxnSignature() const
    {
        if (tx_->isFieldPresent(sfTxnSignature))
            return tx_->getFieldVL(sfTxnSignature);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has a transaction signature.
     * @return true if sfTxnSignature is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasTxnSignature() const
    {
        return tx_->isFieldPresent(sfTxnSignature);
    }

    /**
     * @brief Get the signers array (sfSigners).
     *
     * This field is OPTIONAL and contains the list of signers for multi-signed transactions.
     * @note This is an untyped field (STArray).
     * @return A reference wrapper to the signers array if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getSigners() const
    {
        if (tx_->isFieldPresent(sfSigners))
            return tx_->getFieldArray(sfSigners);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has signers.
     * @return true if sfSigners is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasSigners() const
    {
        return tx_->isFieldPresent(sfSigners);
    }

    /**
     * @brief Get the network ID (sfNetworkID).
     *
     * This field is OPTIONAL and identifies the network this transaction is intended for.
     * @return The network ID if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<uint32_t>
    getNetworkID() const
    {
        if (tx_->isFieldPresent(sfNetworkID))
            return tx_->at(sfNetworkID);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has a network ID.
     * @return true if sfNetworkID is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasNetworkID() const
    {
        return tx_->isFieldPresent(sfNetworkID);
    }

    /**
     * @brief Get the delegate account (sfDelegate).
     *
     * This field is OPTIONAL and specifies a delegate account for the transaction.
     * @return The delegate account ID if present, std::nullopt otherwise
     */
    [[nodiscard]]
    std::optional<AccountID>
    getDelegate() const
    {
        if (tx_->isFieldPresent(sfDelegate))
            return tx_->at(sfDelegate);
        return std::nullopt;
    }

    /**
     * @brief Check if the transaction has a delegate account.
     * @return true if sfDelegate is present, false otherwise
     */
    [[nodiscard]]
    bool
    hasDelegate() const
    {
        return tx_->isFieldPresent(sfDelegate);
    }

    /**
     * @brief Get the underlying STTx object.
     *
     * Provides direct access to the wrapped transaction object for cases
     * where the type-safe accessors are insufficient.
     * @return A constant reference to the underlying STTx object
     */
    [[nodiscard]]
    std::shared_ptr<STTx const>
    getSTTx() const
    {
        return tx_;
    }

protected:
    /** @brief The underlying transaction object being wrapped. */
    std::shared_ptr<STTx const> tx_;
};

}  // namespace xrpl::transactions
