
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/st.h>
#ifndef RIPPLE_TX_TX_CONSEQUENCES_H_INCLUDED
#define RIPPLE_TX_TX_CONSEQUENCES_H_INCLUDED

namespace ripple {

enum ConsequencesFactoryType { Normal, Blocker, Custom };

/** Class describing the consequences to the account
    of applying a transaction if the transaction consumes
    the maximum XRP allowed.
*/
class TxConsequences
{
public:
    /// Describes how the transaction affects subsequent
    /// transactions
    enum Category {
        /// Moves currency around, creates offers, etc.
        normal = 0,
        /// Affects the ability of subsequent transactions
        /// to claim a fee. Eg. `SetRegularKey`
        blocker
    };

private:
    /// Describes how the transaction affects subsequent
    /// transactions
    bool isBlocker_;
    /// Transaction fee
    XRPAmount fee_;
    /// Does NOT include the fee.
    XRPAmount potentialSpend_;
    /// SeqProxy of transaction.
    SeqProxy seqProx_;
    /// Number of sequences consumed.
    std::uint32_t sequencesConsumed_;

public:
    // Constructor if preflight returns a value other than tesSUCCESS.
    // Asserts if tesSUCCESS is passed.
    explicit TxConsequences(NotTEC pfresult);

    /// Constructor if the STTx has no notable consequences for the TxQ.
    explicit TxConsequences(STTx const& tx);

    /// Constructor for a blocker.
    TxConsequences(STTx const& tx, Category category);

    /// Constructor for an STTx that may consume more XRP than the fee.
    TxConsequences(STTx const& tx, XRPAmount potentialSpend);

    /// Constructor for an STTx that consumes more than the usual sequences.
    TxConsequences(STTx const& tx, std::uint32_t sequencesConsumed);

    /// Copy constructor
    TxConsequences(TxConsequences const&) = default;
    /// Copy assignment operator
    TxConsequences&
    operator=(TxConsequences const&) = default;
    /// Move constructor
    TxConsequences(TxConsequences&&) = default;
    /// Move assignment operator
    TxConsequences&
    operator=(TxConsequences&&) = default;

    /// Fee
    XRPAmount
    fee() const
    {
        return fee_;
    }

    /// Potential Spend
    XRPAmount const&
    potentialSpend() const
    {
        return potentialSpend_;
    }

    /// SeqProxy
    SeqProxy
    seqProxy() const
    {
        return seqProx_;
    }

    /// Sequences consumed
    std::uint32_t
    sequencesConsumed() const
    {
        return sequencesConsumed_;
    }

    /// Returns true if the transaction is a blocker.
    bool
    isBlocker() const
    {
        return isBlocker_;
    }

    // Return the SeqProxy that would follow this.
    SeqProxy
    followingSeq() const
    {
        SeqProxy following = seqProx_;
        following.advanceBy(sequencesConsumed());
        return following;
    }
};

}  // namespace ripple

#endif
