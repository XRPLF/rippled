import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.LedgerEntries.Loan

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_loan_empty]
def lean_loan_empty (_ : Unit) : Loan := Loan.empty

@[export lean_loan_key_get]
def lean_loan_key_get (l : Loan) : UInt256 := l.key
@[export lean_loan_key_set]
def lean_loan_key_set (l : Loan) (key : UInt256) : Loan := { l with key }

@[export lean_loan_flags_get]
def lean_loan_flags_get (l : Loan) : UInt32 := l.flags
@[export lean_loan_flags_set]
def lean_loan_flags_set (l : Loan) (flags : UInt32) : Loan := { l with flags }

@[export lean_loan_previous_txn_id_get]
def lean_loan_previous_txn_id_get (l : Loan) : UInt256 := l.previousTxnID
@[export lean_loan_previous_txn_id_set]
def lean_loan_previous_txn_id_set (l : Loan) (previousTxnID : UInt256) : Loan := { l with previousTxnID }

@[export lean_loan_previous_txn_lgr_seq_get]
def lean_loan_previous_txn_lgr_seq_get (l : Loan) : UInt32 := l.previousTxnLgrSeq
@[export lean_loan_previous_txn_lgr_seq_set]
def lean_loan_previous_txn_lgr_seq_set (l : Loan) (previousTxnLgrSeq : UInt32) : Loan :=
  { l with previousTxnLgrSeq }

@[export lean_loan_owner_node_get]
def lean_loan_owner_node_get (l : Loan) : UInt64 := l.ownerNode
@[export lean_loan_owner_node_set]
def lean_loan_owner_node_set (l : Loan) (ownerNode : UInt64) : Loan := { l with ownerNode }

@[export lean_loan_loan_broker_node_get]
def lean_loan_loan_broker_node_get (l : Loan) : UInt64 := l.loanBrokerNode
@[export lean_loan_loan_broker_node_set]
def lean_loan_loan_broker_node_set (l : Loan) (loanBrokerNode : UInt64) : Loan := { l with loanBrokerNode }

@[export lean_loan_loan_broker_id_get]
def lean_loan_loan_broker_id_get (l : Loan) : UInt256 := l.loanBrokerID
@[export lean_loan_loan_broker_id_set]
def lean_loan_loan_broker_id_set (l : Loan) (loanBrokerID : UInt256) : Loan := { l with loanBrokerID }

@[export lean_loan_loan_sequence_get]
def lean_loan_loan_sequence_get (l : Loan) : UInt32 := l.loanSequence
@[export lean_loan_loan_sequence_set]
def lean_loan_loan_sequence_set (l : Loan) (loanSequence : UInt32) : Loan := { l with loanSequence }

@[export lean_loan_borrower_get]
def lean_loan_borrower_get (l : Loan) : AccountID := l.borrower
@[export lean_loan_borrower_set]
def lean_loan_borrower_set (l : Loan) (borrower : AccountID) : Loan := { l with borrower }

@[export lean_loan_loan_origination_fee_get]
def lean_loan_loan_origination_fee_get (l : Loan) : Option STNumber := l.loanOriginationFee
@[export lean_loan_loan_origination_fee_set]
def lean_loan_loan_origination_fee_set (l : Loan) (loanOriginationFee : Option STNumber) : Loan :=
  { l with loanOriginationFee }

@[export lean_loan_loan_service_fee_get]
def lean_loan_loan_service_fee_get (l : Loan) : Option STNumber := l.loanServiceFee
@[export lean_loan_loan_service_fee_set]
def lean_loan_loan_service_fee_set (l : Loan) (loanServiceFee : Option STNumber) : Loan :=
  { l with loanServiceFee }

@[export lean_loan_late_payment_fee_get]
def lean_loan_late_payment_fee_get (l : Loan) : Option STNumber := l.latePaymentFee
@[export lean_loan_late_payment_fee_set]
def lean_loan_late_payment_fee_set (l : Loan) (latePaymentFee : Option STNumber) : Loan :=
  { l with latePaymentFee }

@[export lean_loan_close_payment_fee_get]
def lean_loan_close_payment_fee_get (l : Loan) : Option STNumber := l.closePaymentFee
@[export lean_loan_close_payment_fee_set]
def lean_loan_close_payment_fee_set (l : Loan) (closePaymentFee : Option STNumber) : Loan :=
  { l with closePaymentFee }

@[export lean_loan_overpayment_fee_get]
def lean_loan_overpayment_fee_get (l : Loan) : UInt32 := l.overpaymentFee
@[export lean_loan_overpayment_fee_set]
def lean_loan_overpayment_fee_set (l : Loan) (overpaymentFee : UInt32) : Loan := { l with overpaymentFee }

@[export lean_loan_interest_rate_get]
def lean_loan_interest_rate_get (l : Loan) : UInt32 := l.interestRate
@[export lean_loan_interest_rate_set]
def lean_loan_interest_rate_set (l : Loan) (interestRate : UInt32) : Loan := { l with interestRate }

@[export lean_loan_late_interest_rate_get]
def lean_loan_late_interest_rate_get (l : Loan) : UInt32 := l.lateInterestRate
@[export lean_loan_late_interest_rate_set]
def lean_loan_late_interest_rate_set (l : Loan) (lateInterestRate : UInt32) : Loan := { l with lateInterestRate }

@[export lean_loan_close_interest_rate_get]
def lean_loan_close_interest_rate_get (l : Loan) : UInt32 := l.closeInterestRate
@[export lean_loan_close_interest_rate_set]
def lean_loan_close_interest_rate_set (l : Loan) (closeInterestRate : UInt32) : Loan :=
  { l with closeInterestRate }

@[export lean_loan_overpayment_interest_rate_get]
def lean_loan_overpayment_interest_rate_get (l : Loan) : UInt32 := l.overpaymentInterestRate
@[export lean_loan_overpayment_interest_rate_set]
def lean_loan_overpayment_interest_rate_set (l : Loan) (overpaymentInterestRate : UInt32) : Loan :=
  { l with overpaymentInterestRate }

@[export lean_loan_start_date_get]
def lean_loan_start_date_get (l : Loan) : UInt32 := l.startDate
@[export lean_loan_start_date_set]
def lean_loan_start_date_set (l : Loan) (startDate : UInt32) : Loan := { l with startDate }

@[export lean_loan_payment_interval_get]
def lean_loan_payment_interval_get (l : Loan) : UInt32 := l.paymentInterval
@[export lean_loan_payment_interval_set]
def lean_loan_payment_interval_set (l : Loan) (paymentInterval : UInt32) : Loan := { l with paymentInterval }

@[export lean_loan_grace_period_get]
def lean_loan_grace_period_get (l : Loan) : UInt32 := l.gracePeriod
@[export lean_loan_grace_period_set]
def lean_loan_grace_period_set (l : Loan) (gracePeriod : UInt32) : Loan := { l with gracePeriod }

@[export lean_loan_previous_payment_due_date_get]
def lean_loan_previous_payment_due_date_get (l : Loan) : UInt32 := l.previousPaymentDueDate
@[export lean_loan_previous_payment_due_date_set]
def lean_loan_previous_payment_due_date_set (l : Loan) (previousPaymentDueDate : UInt32) : Loan :=
  { l with previousPaymentDueDate }

@[export lean_loan_next_payment_due_date_get]
def lean_loan_next_payment_due_date_get (l : Loan) : UInt32 := l.nextPaymentDueDate
@[export lean_loan_next_payment_due_date_set]
def lean_loan_next_payment_due_date_set (l : Loan) (nextPaymentDueDate : UInt32) : Loan :=
  { l with nextPaymentDueDate }

@[export lean_loan_payment_remaining_get]
def lean_loan_payment_remaining_get (l : Loan) : UInt32 := l.paymentRemaining
@[export lean_loan_payment_remaining_set]
def lean_loan_payment_remaining_set (l : Loan) (paymentRemaining : UInt32) : Loan := { l with paymentRemaining }

@[export lean_loan_periodic_payment_get]
def lean_loan_periodic_payment_get (l : Loan) : Option STNumber := l.periodicPayment
@[export lean_loan_periodic_payment_set]
def lean_loan_periodic_payment_set (l : Loan) (periodicPayment : Option STNumber) : Loan :=
  { l with periodicPayment }

@[export lean_loan_principal_outstanding_get]
def lean_loan_principal_outstanding_get (l : Loan) : Option STNumber := l.principalOutstanding
@[export lean_loan_principal_outstanding_set]
def lean_loan_principal_outstanding_set (l : Loan) (principalOutstanding : Option STNumber) : Loan :=
  { l with principalOutstanding }

@[export lean_loan_total_value_outstanding_get]
def lean_loan_total_value_outstanding_get (l : Loan) : Option STNumber := l.totalValueOutstanding
@[export lean_loan_total_value_outstanding_set]
def lean_loan_total_value_outstanding_set (l : Loan) (totalValueOutstanding : Option STNumber) : Loan :=
  { l with totalValueOutstanding }

@[export lean_loan_management_fee_outstanding_get]
def lean_loan_management_fee_outstanding_get (l : Loan) : Option STNumber := l.managementFeeOutstanding
@[export lean_loan_management_fee_outstanding_set]
def lean_loan_management_fee_outstanding_set (l : Loan) (managementFeeOutstanding : Option STNumber) : Loan :=
  { l with managementFeeOutstanding }

@[export lean_loan_loan_scale_get]
def lean_loan_loan_scale_get (l : Loan) : Int32 := l.loanScale
@[export lean_loan_loan_scale_set]
def lean_loan_loan_scale_set (l : Loan) (loanScale : Int32) : Loan := { l with loanScale }

@[export lean_loan_associate_asset]
def lean_loan_associate_asset (l : Loan) (asset : Asset) (mode : UInt8) : Except String Loan :=
  l.associateAsset asset (decodeMode mode)

end XRPL.FFI
