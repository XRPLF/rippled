import XRPL.Model.Protocol.LedgerEntries.MPToken


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_mptoken_empty]
def lean_mptoken_empty (_ : Unit) : MPToken := MPToken.empty

@[export lean_mptoken_key_get]
def lean_mptoken_key_get (m : MPToken) : UInt256 := m.key
@[export lean_mptoken_key_set]
def lean_mptoken_key_set (m : MPToken) (key : UInt256) : MPToken := { m with key }

@[export lean_mptoken_flags_get]
def lean_mptoken_flags_get (m : MPToken) : UInt32 := m.flags
@[export lean_mptoken_flags_set]
def lean_mptoken_flags_set (m : MPToken) (flags : UInt32) : MPToken := { m with flags }

@[export lean_mptoken_account_get]
def lean_mptoken_account_get (m : MPToken) : AccountID := m.account
@[export lean_mptoken_account_set]
def lean_mptoken_account_set (m : MPToken) (account : AccountID) : MPToken := { m with account }

@[export lean_mptoken_issuance_id_get]
def lean_mptoken_issuance_id_get (m : MPToken) : MPTID := m.mptokenIssuanceID
@[export lean_mptoken_issuance_id_set]
def lean_mptoken_issuance_id_set (m : MPToken) (mptokenIssuanceID : MPTID) : MPToken :=
  { m with mptokenIssuanceID }

@[export lean_mptoken_amount_get]
def lean_mptoken_amount_get (m : MPToken) : UInt64 := m.mptAmount
@[export lean_mptoken_amount_set]
def lean_mptoken_amount_set (m : MPToken) (mptAmount : UInt64) : MPToken := { m with mptAmount }

@[export lean_mptoken_locked_amount_get]
def lean_mptoken_locked_amount_get (m : MPToken) : Option UInt64 := m.lockedAmount
@[export lean_mptoken_locked_amount_set]
def lean_mptoken_locked_amount_set (m : MPToken) (lockedAmount : Option UInt64) : MPToken :=
  { m with lockedAmount }

@[export lean_mptoken_owner_node_get]
def lean_mptoken_owner_node_get (m : MPToken) : UInt64 := m.ownerNode
@[export lean_mptoken_owner_node_set]
def lean_mptoken_owner_node_set (m : MPToken) (ownerNode : UInt64) : MPToken := { m with ownerNode }

@[export lean_mptoken_previous_txn_id_get]
def lean_mptoken_previous_txn_id_get (m : MPToken) : UInt256 := m.previousTxnID
@[export lean_mptoken_previous_txn_id_set]
def lean_mptoken_previous_txn_id_set (m : MPToken) (previousTxnID : UInt256) : MPToken :=
  { m with previousTxnID }

@[export lean_mptoken_previous_txn_lgr_seq_get]
def lean_mptoken_previous_txn_lgr_seq_get (m : MPToken) : UInt32 := m.previousTxnLgrSeq
@[export lean_mptoken_previous_txn_lgr_seq_set]
def lean_mptoken_previous_txn_lgr_seq_set (m : MPToken) (previousTxnLgrSeq : UInt32) : MPToken :=
  { m with previousTxnLgrSeq }

end XRPL.FFI
