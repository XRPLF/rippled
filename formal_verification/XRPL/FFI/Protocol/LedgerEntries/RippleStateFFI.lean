import XRPL.Model.Protocol.LedgerEntries.RippleState


namespace XRPL.FFI

open XRPL.Model.Protocol (STAmount RippleState UInt256)

@[export lean_ripple_state_empty]
def lean_ripple_state_empty (_ : Unit) : RippleState := RippleState.empty

@[export lean_ripple_state_key_get]
def lean_ripple_state_key_get (r : RippleState) : UInt256 := r.key
@[export lean_ripple_state_key_set]
def lean_ripple_state_key_set (r : RippleState) (key : UInt256) : RippleState := { r with key }

@[export lean_ripple_state_flags_get]
def lean_ripple_state_flags_get (r : RippleState) : UInt32 := r.flags
@[export lean_ripple_state_flags_set]
def lean_ripple_state_flags_set (r : RippleState) (flags : UInt32) : RippleState := { r with flags }

@[export lean_ripple_state_balance_get]
def lean_ripple_state_balance_get (r : RippleState) : STAmount := r.balance
@[export lean_ripple_state_balance_set]
def lean_ripple_state_balance_set (r : RippleState) (balance : STAmount) : RippleState := { r with balance }

@[export lean_ripple_state_low_limit_get]
def lean_ripple_state_low_limit_get (r : RippleState) : STAmount := r.lowLimit
@[export lean_ripple_state_low_limit_set]
def lean_ripple_state_low_limit_set (r : RippleState) (lowLimit : STAmount) : RippleState := { r with lowLimit }

@[export lean_ripple_state_high_limit_get]
def lean_ripple_state_high_limit_get (r : RippleState) : STAmount := r.highLimit
@[export lean_ripple_state_high_limit_set]
def lean_ripple_state_high_limit_set (r : RippleState) (highLimit : STAmount) : RippleState := { r with highLimit }

@[export lean_ripple_state_previous_txn_id_get]
def lean_ripple_state_previous_txn_id_get (r : RippleState) : UInt256 := r.previousTxnID
@[export lean_ripple_state_previous_txn_id_set]
def lean_ripple_state_previous_txn_id_set (r : RippleState) (previousTxnID : UInt256) : RippleState :=
  { r with previousTxnID }

@[export lean_ripple_state_previous_txn_lgr_seq_get]
def lean_ripple_state_previous_txn_lgr_seq_get (r : RippleState) : UInt32 := r.previousTxnLgrSeq
@[export lean_ripple_state_previous_txn_lgr_seq_set]
def lean_ripple_state_previous_txn_lgr_seq_set (r : RippleState) (previousTxnLgrSeq : UInt32) : RippleState :=
  { r with previousTxnLgrSeq }

@[export lean_ripple_state_low_node_get]
def lean_ripple_state_low_node_get (r : RippleState) : Option UInt64 := r.lowNode
@[export lean_ripple_state_low_node_set]
def lean_ripple_state_low_node_set (r : RippleState) (lowNode : Option UInt64) : RippleState := { r with lowNode }

@[export lean_ripple_state_low_quality_in_get]
def lean_ripple_state_low_quality_in_get (r : RippleState) : Option UInt32 := r.lowQualityIn
@[export lean_ripple_state_low_quality_in_set]
def lean_ripple_state_low_quality_in_set (r : RippleState) (lowQualityIn : Option UInt32) : RippleState :=
  { r with lowQualityIn }

@[export lean_ripple_state_low_quality_out_get]
def lean_ripple_state_low_quality_out_get (r : RippleState) : Option UInt32 := r.lowQualityOut
@[export lean_ripple_state_low_quality_out_set]
def lean_ripple_state_low_quality_out_set (r : RippleState) (lowQualityOut : Option UInt32) : RippleState :=
  { r with lowQualityOut }

@[export lean_ripple_state_high_node_get]
def lean_ripple_state_high_node_get (r : RippleState) : Option UInt64 := r.highNode
@[export lean_ripple_state_high_node_set]
def lean_ripple_state_high_node_set (r : RippleState) (highNode : Option UInt64) : RippleState := { r with highNode }

@[export lean_ripple_state_high_quality_in_get]
def lean_ripple_state_high_quality_in_get (r : RippleState) : Option UInt32 := r.highQualityIn
@[export lean_ripple_state_high_quality_in_set]
def lean_ripple_state_high_quality_in_set (r : RippleState) (highQualityIn : Option UInt32) : RippleState :=
  { r with highQualityIn }

@[export lean_ripple_state_high_quality_out_get]
def lean_ripple_state_high_quality_out_get (r : RippleState) : Option UInt32 := r.highQualityOut
@[export lean_ripple_state_high_quality_out_set]
def lean_ripple_state_high_quality_out_set (r : RippleState) (highQualityOut : Option UInt32) : RippleState :=
  { r with highQualityOut }

end XRPL.FFI
