import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Protocol.STTakesAsset


namespace XRPL.Model.Protocol

-- NUMBER fields are SoeDefault in rippled but kept `Option` here (none ≡ default),
-- to match the model's STNumber convention and `associateAsset`.
structure Loan where
  key : UInt256 := 0
  flags : UInt32 := 0
  previousTxnID : UInt256 := 0
  previousTxnLgrSeq : UInt32 := 0
  ownerNode : UInt64 := 0
  loanBrokerNode : UInt64 := 0
  loanBrokerID : UInt256 := 0
  loanSequence : UInt32 := 0
  borrower : AccountID := xrpAccount
  loanOriginationFee : Option STNumber := none
  loanServiceFee : Option STNumber := none
  latePaymentFee : Option STNumber := none
  closePaymentFee : Option STNumber := none
  overpaymentFee : UInt32 := 0
  interestRate : UInt32 := 0
  lateInterestRate : UInt32 := 0
  closeInterestRate : UInt32 := 0
  overpaymentInterestRate : UInt32 := 0
  startDate : UInt32 := 0
  paymentInterval : UInt32 := 0
  gracePeriod : UInt32 := 0
  previousPaymentDueDate : UInt32 := 0
  nextPaymentDueDate : UInt32 := 0
  paymentRemaining : UInt32 := 0
  periodicPayment : Option STNumber := none
  principalOutstanding : Option STNumber := none
  totalValueOutstanding : Option STNumber := none
  managementFeeOutstanding : Option STNumber := none
  loanScale : Int32 := 0

def Loan.empty : Loan := {}

def Loan.associateAsset (l : Loan) (asset : Asset) (mode : rounding_mode)
    : Except String Loan := do
  let principalOutstanding ← associateNumberField asset mode l.principalOutstanding
  let totalValueOutstanding ← associateNumberField asset mode l.totalValueOutstanding
  let managementFeeOutstanding ← associateNumberField asset mode l.managementFeeOutstanding
  return { l with principalOutstanding, totalValueOutstanding, managementFeeOutstanding }

end XRPL.Model.Protocol
