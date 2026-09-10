#include <xrpl/ledger/helpers/ProposalHelpers.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

namespace xrpl::test {

namespace {

// A bare STObject carrying only sfTransactionType. isValidProposal accepts an
// STObject (not an STTx) precisely so that this file's defense-in-depth
// checks can be tested without the surrounding STTx format validation.
inline STObject
txOfType(std::uint16_t txType)
{
    STObject tx(sfGeneric);
    tx.setFieldU16(sfTransactionType, txType);
    return tx;
}

// A bare Batch STObject wrapping the given inner transaction as its single
// sfRawTransactions entry.
inline STObject
batchWrapping(STObject inner)
{
    STArray rawTxns(sfRawTransactions);
    rawTxns.push_back(std::move(inner));

    STObject batch(sfGeneric);
    batch.setFieldU16(sfTransactionType, ttBATCH);
    batch.setFieldArray(sfRawTransactions, rawTxns);
    return batch;
}

}  // namespace

// The happy path — an ordinary Payment is independently submittable.
TEST(ProposalHelpers, PlainPaymentIsValid)
{
    EXPECT_TRUE(proposal::isValidProposal(txOfType(ttPAYMENT)));
}

// A nested TransactionProposalCreate. In practice STTx construction rejects
// this earlier (the payload lacks TransactionProposalCreate's own template
// fields), but the defense here re-checks that guard so the two cannot
// drift apart.
TEST(ProposalHelpers, NestedProposalIsRejected)
{
    EXPECT_FALSE(proposal::isValidProposal(txOfType(ttTRANSACTION_PROPOSAL_CREATE)));
}

// Any pseudo-transaction — see STTx::isPseudoTx. Also normally caught earlier
// by STTx construction / preflight0.
TEST(ProposalHelpers, PseudoTxIsRejected)
{
    EXPECT_FALSE(proposal::isValidProposal(txOfType(ttAMENDMENT)));
    EXPECT_FALSE(proposal::isValidProposal(txOfType(ttFEE)));
    EXPECT_FALSE(proposal::isValidProposal(txOfType(ttUNL_MODIFY)));
}

// tfInnerBatchTxn marks a transaction as an inner leg of an enclosing Batch,
// so it must never stand on its own as a proposed transaction. preflight0
// rejects the standalone case with temINVALID_INNER_BATCH before we get
// here; the guard is re-checked so the two cannot drift apart.
TEST(ProposalHelpers, InnerBatchFlagIsRejected)
{
    STObject tx = txOfType(ttPAYMENT);
    tx.setFieldU32(sfFlags, tfInnerBatchTxn);
    EXPECT_FALSE(proposal::isValidProposal(tx));
}

// A Flags value that is present but does not include tfInnerBatchTxn must
// not be rejected — the check is bit-specific, not "any flag present".
TEST(ProposalHelpers, OtherFlagsAreAccepted)
{
    STObject tx = txOfType(ttPAYMENT);
    tx.setFieldU32(sfFlags, tfFullyCanonicalSig);
    EXPECT_TRUE(proposal::isValidProposal(tx));
}

// A Batch wrapping a plain inner is fine — the loop is only there to catch
// specifically forbidden inner types.
TEST(ProposalHelpers, BatchWithPlainInnerIsValid)
{
    EXPECT_TRUE(proposal::isValidProposal(batchWrapping(txOfType(ttPAYMENT))));
}

// A Batch whose inner is itself a proposal must be rejected.
TEST(ProposalHelpers, BatchWithNestedProposalInnerIsRejected)
{
    EXPECT_FALSE(proposal::isValidProposal(batchWrapping(txOfType(ttTRANSACTION_PROPOSAL_CREATE))));
}

// A Batch whose inner is a pseudo-transaction must be rejected.
TEST(ProposalHelpers, BatchWithPseudoInnerIsRejected)
{
    EXPECT_FALSE(proposal::isValidProposal(batchWrapping(txOfType(ttAMENDMENT))));
}

// A Batch with no sfRawTransactions field skips the inner-loop entirely.
// Not something the transactor would ever emit, but the branch exists in the
// helper (the field is optional at the STObject level) and should hold.
TEST(ProposalHelpers, BatchWithoutRawTransactionsIsValid)
{
    EXPECT_TRUE(proposal::isValidProposal(txOfType(ttBATCH)));
}

}  // namespace xrpl::test
