import XRPL.Model.Protocol.LedgerEntries.MPTokenIssuance


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_mptoken_issuance_empty]
def lean_mptoken_issuance_empty (_ : Unit) : MPTokenIssuance := MPTokenIssuance.empty

@[export lean_mptoken_issuance_key_get]
def lean_mptoken_issuance_key_get (i : MPTokenIssuance) : UInt256 := i.key
@[export lean_mptoken_issuance_key_set]
def lean_mptoken_issuance_key_set (i : MPTokenIssuance) (key : UInt256) : MPTokenIssuance := { i with key }

@[export lean_mptoken_issuance_flags_get]
def lean_mptoken_issuance_flags_get (i : MPTokenIssuance) : UInt32 := i.flags
@[export lean_mptoken_issuance_flags_set]
def lean_mptoken_issuance_flags_set (i : MPTokenIssuance) (flags : UInt32) : MPTokenIssuance := { i with flags }

@[export lean_mptoken_issuance_issuer_get]
def lean_mptoken_issuance_issuer_get (i : MPTokenIssuance) : AccountID := i.issuer
@[export lean_mptoken_issuance_issuer_set]
def lean_mptoken_issuance_issuer_set (i : MPTokenIssuance) (issuer : AccountID) : MPTokenIssuance :=
  { i with issuer }

@[export lean_mptoken_issuance_sequence_get]
def lean_mptoken_issuance_sequence_get (i : MPTokenIssuance) : UInt32 := i.sequence
@[export lean_mptoken_issuance_sequence_set]
def lean_mptoken_issuance_sequence_set (i : MPTokenIssuance) (sequence : UInt32) : MPTokenIssuance :=
  { i with sequence }

@[export lean_mptoken_issuance_transfer_fee_get]
def lean_mptoken_issuance_transfer_fee_get (i : MPTokenIssuance) : UInt16 := i.transferFee
@[export lean_mptoken_issuance_transfer_fee_set]
def lean_mptoken_issuance_transfer_fee_set (i : MPTokenIssuance) (transferFee : UInt16) : MPTokenIssuance :=
  { i with transferFee }

@[export lean_mptoken_issuance_owner_node_get]
def lean_mptoken_issuance_owner_node_get (i : MPTokenIssuance) : UInt64 := i.ownerNode
@[export lean_mptoken_issuance_owner_node_set]
def lean_mptoken_issuance_owner_node_set (i : MPTokenIssuance) (ownerNode : UInt64) : MPTokenIssuance :=
  { i with ownerNode }

@[export lean_mptoken_issuance_asset_scale_get]
def lean_mptoken_issuance_asset_scale_get (i : MPTokenIssuance) : UInt8 := i.assetScale
@[export lean_mptoken_issuance_asset_scale_set]
def lean_mptoken_issuance_asset_scale_set (i : MPTokenIssuance) (assetScale : UInt8) : MPTokenIssuance :=
  { i with assetScale }

@[export lean_mptoken_issuance_maximum_amount_get]
def lean_mptoken_issuance_maximum_amount_get (i : MPTokenIssuance) : Option UInt64 := i.maximumAmount
@[export lean_mptoken_issuance_maximum_amount_set]
def lean_mptoken_issuance_maximum_amount_set (i : MPTokenIssuance) (maximumAmount : Option UInt64) : MPTokenIssuance :=
  { i with maximumAmount }

@[export lean_mptoken_issuance_outstanding_amount_get]
def lean_mptoken_issuance_outstanding_amount_get (i : MPTokenIssuance) : UInt64 := i.outstandingAmount
@[export lean_mptoken_issuance_outstanding_amount_set]
def lean_mptoken_issuance_outstanding_amount_set (i : MPTokenIssuance) (outstandingAmount : UInt64) : MPTokenIssuance :=
  { i with outstandingAmount }

@[export lean_mptoken_issuance_locked_amount_get]
def lean_mptoken_issuance_locked_amount_get (i : MPTokenIssuance) : Option UInt64 := i.lockedAmount
@[export lean_mptoken_issuance_locked_amount_set]
def lean_mptoken_issuance_locked_amount_set (i : MPTokenIssuance) (lockedAmount : Option UInt64) : MPTokenIssuance :=
  { i with lockedAmount }

@[export lean_mptoken_issuance_metadata_get]
def lean_mptoken_issuance_metadata_get (i : MPTokenIssuance) : Option ByteArray := i.mptokenMetadata.map (⟨·.toArray⟩)
@[export lean_mptoken_issuance_metadata_set]
def lean_mptoken_issuance_metadata_set (i : MPTokenIssuance) (mptokenMetadata : Option ByteArray) : MPTokenIssuance :=
  { i with mptokenMetadata := mptokenMetadata.map (·.toList) }

@[export lean_mptoken_issuance_previous_txn_id_get]
def lean_mptoken_issuance_previous_txn_id_get (i : MPTokenIssuance) : UInt256 := i.previousTxnID
@[export lean_mptoken_issuance_previous_txn_id_set]
def lean_mptoken_issuance_previous_txn_id_set (i : MPTokenIssuance) (previousTxnID : UInt256) : MPTokenIssuance :=
  { i with previousTxnID }

@[export lean_mptoken_issuance_previous_txn_lgr_seq_get]
def lean_mptoken_issuance_previous_txn_lgr_seq_get (i : MPTokenIssuance) : UInt32 := i.previousTxnLgrSeq
@[export lean_mptoken_issuance_previous_txn_lgr_seq_set]
def lean_mptoken_issuance_previous_txn_lgr_seq_set (i : MPTokenIssuance) (previousTxnLgrSeq : UInt32) : MPTokenIssuance :=
  { i with previousTxnLgrSeq }

@[export lean_mptoken_issuance_domain_id_get]
def lean_mptoken_issuance_domain_id_get (i : MPTokenIssuance) : Option UInt256 := i.domainID
@[export lean_mptoken_issuance_domain_id_set]
def lean_mptoken_issuance_domain_id_set (i : MPTokenIssuance) (domainID : Option UInt256) : MPTokenIssuance :=
  { i with domainID }

@[export lean_mptoken_issuance_mutable_flags_get]
def lean_mptoken_issuance_mutable_flags_get (i : MPTokenIssuance) : UInt32 := i.mutableFlags
@[export lean_mptoken_issuance_mutable_flags_set]
def lean_mptoken_issuance_mutable_flags_set (i : MPTokenIssuance) (mutableFlags : UInt32) : MPTokenIssuance :=
  { i with mutableFlags }

@[export lean_mptoken_issuance_reference_holding_get]
def lean_mptoken_issuance_reference_holding_get (i : MPTokenIssuance) : Option UInt256 := i.referenceHolding
@[export lean_mptoken_issuance_reference_holding_set]
def lean_mptoken_issuance_reference_holding_set (i : MPTokenIssuance) (referenceHolding : Option UInt256) : MPTokenIssuance :=
  { i with referenceHolding }

@[export lean_mptoken_issuance_mpt_id_get]
def lean_mptoken_issuance_mpt_id_get (i : MPTokenIssuance) : MPTID := i.mptID

end XRPL.FFI
