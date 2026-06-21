import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.LedgerEntries.LoanBroker

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_loan_broker_empty]
def lean_loan_broker_empty (_ : Unit) : LoanBroker := LoanBroker.empty

@[export lean_loan_broker_key_get]
def lean_loan_broker_key_get (b : LoanBroker) : UInt256 := b.key
@[export lean_loan_broker_key_set]
def lean_loan_broker_key_set (b : LoanBroker) (key : UInt256) : LoanBroker := { b with key }

@[export lean_loan_broker_flags_get]
def lean_loan_broker_flags_get (b : LoanBroker) : UInt32 := b.flags
@[export lean_loan_broker_flags_set]
def lean_loan_broker_flags_set (b : LoanBroker) (flags : UInt32) : LoanBroker := { b with flags }

@[export lean_loan_broker_previous_txn_id_get]
def lean_loan_broker_previous_txn_id_get (b : LoanBroker) : UInt256 := b.previousTxnID
@[export lean_loan_broker_previous_txn_id_set]
def lean_loan_broker_previous_txn_id_set (b : LoanBroker) (previousTxnID : UInt256) : LoanBroker :=
  { b with previousTxnID }

@[export lean_loan_broker_previous_txn_lgr_seq_get]
def lean_loan_broker_previous_txn_lgr_seq_get (b : LoanBroker) : UInt32 := b.previousTxnLgrSeq
@[export lean_loan_broker_previous_txn_lgr_seq_set]
def lean_loan_broker_previous_txn_lgr_seq_set (b : LoanBroker) (previousTxnLgrSeq : UInt32) : LoanBroker :=
  { b with previousTxnLgrSeq }

@[export lean_loan_broker_sequence_get]
def lean_loan_broker_sequence_get (b : LoanBroker) : UInt32 := b.sequence
@[export lean_loan_broker_sequence_set]
def lean_loan_broker_sequence_set (b : LoanBroker) (sequence : UInt32) : LoanBroker := { b with sequence }

@[export lean_loan_broker_owner_node_get]
def lean_loan_broker_owner_node_get (b : LoanBroker) : UInt64 := b.ownerNode
@[export lean_loan_broker_owner_node_set]
def lean_loan_broker_owner_node_set (b : LoanBroker) (ownerNode : UInt64) : LoanBroker := { b with ownerNode }

@[export lean_loan_broker_vault_node_get]
def lean_loan_broker_vault_node_get (b : LoanBroker) : UInt64 := b.vaultNode
@[export lean_loan_broker_vault_node_set]
def lean_loan_broker_vault_node_set (b : LoanBroker) (vaultNode : UInt64) : LoanBroker := { b with vaultNode }

@[export lean_loan_broker_vault_id_get]
def lean_loan_broker_vault_id_get (b : LoanBroker) : UInt256 := b.vaultID
@[export lean_loan_broker_vault_id_set]
def lean_loan_broker_vault_id_set (b : LoanBroker) (vaultID : UInt256) : LoanBroker := { b with vaultID }

@[export lean_loan_broker_account_get]
def lean_loan_broker_account_get (b : LoanBroker) : AccountID := b.account
@[export lean_loan_broker_account_set]
def lean_loan_broker_account_set (b : LoanBroker) (account : AccountID) : LoanBroker := { b with account }

@[export lean_loan_broker_owner_get]
def lean_loan_broker_owner_get (b : LoanBroker) : AccountID := b.owner
@[export lean_loan_broker_owner_set]
def lean_loan_broker_owner_set (b : LoanBroker) (owner : AccountID) : LoanBroker := { b with owner }

@[export lean_loan_broker_loan_sequence_get]
def lean_loan_broker_loan_sequence_get (b : LoanBroker) : UInt32 := b.loanSequence
@[export lean_loan_broker_loan_sequence_set]
def lean_loan_broker_loan_sequence_set (b : LoanBroker) (loanSequence : UInt32) : LoanBroker :=
  { b with loanSequence }

@[export lean_loan_broker_data_get]
def lean_loan_broker_data_get (b : LoanBroker) : ByteArray := ⟨b.data.toArray⟩
@[export lean_loan_broker_data_set]
def lean_loan_broker_data_set (b : LoanBroker) (data : ByteArray) : LoanBroker :=
  { b with data := data.toList }

@[export lean_loan_broker_management_fee_rate_get]
def lean_loan_broker_management_fee_rate_get (b : LoanBroker) : UInt16 := b.managementFeeRate
@[export lean_loan_broker_management_fee_rate_set]
def lean_loan_broker_management_fee_rate_set (b : LoanBroker) (managementFeeRate : UInt16) : LoanBroker :=
  { b with managementFeeRate }

@[export lean_loan_broker_owner_count_get]
def lean_loan_broker_owner_count_get (b : LoanBroker) : UInt32 := b.ownerCount
@[export lean_loan_broker_owner_count_set]
def lean_loan_broker_owner_count_set (b : LoanBroker) (ownerCount : UInt32) : LoanBroker := { b with ownerCount }

@[export lean_loan_broker_debt_total_get]
def lean_loan_broker_debt_total_get (b : LoanBroker) : Option STNumber := b.debtTotal
@[export lean_loan_broker_debt_total_set]
def lean_loan_broker_debt_total_set (b : LoanBroker) (debtTotal : Option STNumber) : LoanBroker :=
  { b with debtTotal }

@[export lean_loan_broker_debt_maximum_get]
def lean_loan_broker_debt_maximum_get (b : LoanBroker) : Option STNumber := b.debtMaximum
@[export lean_loan_broker_debt_maximum_set]
def lean_loan_broker_debt_maximum_set (b : LoanBroker) (debtMaximum : Option STNumber) : LoanBroker :=
  { b with debtMaximum }

@[export lean_loan_broker_cover_available_get]
def lean_loan_broker_cover_available_get (b : LoanBroker) : Option STNumber := b.coverAvailable
@[export lean_loan_broker_cover_available_set]
def lean_loan_broker_cover_available_set (b : LoanBroker) (coverAvailable : Option STNumber) : LoanBroker :=
  { b with coverAvailable }

@[export lean_loan_broker_cover_rate_minimum_get]
def lean_loan_broker_cover_rate_minimum_get (b : LoanBroker) : UInt32 := b.coverRateMinimum
@[export lean_loan_broker_cover_rate_minimum_set]
def lean_loan_broker_cover_rate_minimum_set (b : LoanBroker) (coverRateMinimum : UInt32) : LoanBroker :=
  { b with coverRateMinimum }

@[export lean_loan_broker_cover_rate_liquidation_get]
def lean_loan_broker_cover_rate_liquidation_get (b : LoanBroker) : UInt32 := b.coverRateLiquidation
@[export lean_loan_broker_cover_rate_liquidation_set]
def lean_loan_broker_cover_rate_liquidation_set (b : LoanBroker) (coverRateLiquidation : UInt32) : LoanBroker :=
  { b with coverRateLiquidation }

@[export lean_loan_broker_associate_asset]
def lean_loan_broker_associate_asset (b : LoanBroker) (asset : Asset) (mode : UInt8) : Except String LoanBroker :=
  b.associateAsset asset (decodeMode mode)

end XRPL.FFI
