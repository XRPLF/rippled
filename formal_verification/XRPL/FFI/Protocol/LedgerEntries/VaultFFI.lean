import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.LedgerEntries.Vault


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_vault_empty]
def lean_vault_empty (_ : Unit) : Vault := Vault.empty

@[export lean_vault_key_get]
def lean_vault_key_get (v : Vault) : UInt256 := v.key
@[export lean_vault_key_set]
def lean_vault_key_set (v : Vault) (key : UInt256) : Vault := { v with key }

@[export lean_vault_flags_get]
def lean_vault_flags_get (v : Vault) : UInt32 := v.flags
@[export lean_vault_flags_set]
def lean_vault_flags_set (v : Vault) (flags : UInt32) : Vault := { v with flags }

@[export lean_vault_previous_txn_id_get]
def lean_vault_previous_txn_id_get (v : Vault) : UInt256 := v.previousTxnID
@[export lean_vault_previous_txn_id_set]
def lean_vault_previous_txn_id_set (v : Vault) (previousTxnID : UInt256) : Vault := { v with previousTxnID }

@[export lean_vault_previous_txn_lgr_seq_get]
def lean_vault_previous_txn_lgr_seq_get (v : Vault) : UInt32 := v.previousTxnLgrSeq
@[export lean_vault_previous_txn_lgr_seq_set]
def lean_vault_previous_txn_lgr_seq_set (v : Vault) (previousTxnLgrSeq : UInt32) : Vault :=
  { v with previousTxnLgrSeq }

@[export lean_vault_sequence_get]
def lean_vault_sequence_get (v : Vault) : UInt32 := v.sequence
@[export lean_vault_sequence_set]
def lean_vault_sequence_set (v : Vault) (sequence : UInt32) : Vault := { v with sequence }

@[export lean_vault_owner_node_get]
def lean_vault_owner_node_get (v : Vault) : UInt64 := v.ownerNode
@[export lean_vault_owner_node_set]
def lean_vault_owner_node_set (v : Vault) (ownerNode : UInt64) : Vault := { v with ownerNode }

@[export lean_vault_owner_get]
def lean_vault_owner_get (v : Vault) : AccountID := v.owner
@[export lean_vault_owner_set]
def lean_vault_owner_set (v : Vault) (owner : AccountID) : Vault := { v with owner }

@[export lean_vault_pseudo_id_get]
def lean_vault_pseudo_id_get (v : Vault) : AccountID := v.pseudoID
@[export lean_vault_pseudo_id_set]
def lean_vault_pseudo_id_set (v : Vault) (pseudoID : AccountID) : Vault := { v with pseudoID }

@[export lean_vault_data_get]
def lean_vault_data_get (v : Vault) : Option ByteArray := v.data.map (⟨·.toArray⟩)
@[export lean_vault_data_set]
def lean_vault_data_set (v : Vault) (data : Option ByteArray) : Vault :=
  { v with data := data.map (·.toList) }

@[export lean_vault_asset_get]
def lean_vault_asset_get (v : Vault) : Asset := v.asset
@[export lean_vault_asset_set]
def lean_vault_asset_set (v : Vault) (asset : Asset) : Vault := { v with asset }

@[export lean_vault_assets_total_get]
def lean_vault_assets_total_get (v : Vault) : Option STNumber := v.assetsTotal
@[export lean_vault_assets_total_set]
def lean_vault_assets_total_set (v : Vault) (assetsTotal : Option STNumber) : Vault := { v with assetsTotal }

@[export lean_vault_assets_available_get]
def lean_vault_assets_available_get (v : Vault) : Option STNumber := v.assetsAvailable
@[export lean_vault_assets_available_set]
def lean_vault_assets_available_set (v : Vault) (assetsAvailable : Option STNumber) : Vault :=
  { v with assetsAvailable }

@[export lean_vault_assets_maximum_get]
def lean_vault_assets_maximum_get (v : Vault) : Option STNumber := v.assetsMaximum
@[export lean_vault_assets_maximum_set]
def lean_vault_assets_maximum_set (v : Vault) (assetsMaximum : Option STNumber) : Vault :=
  { v with assetsMaximum }

@[export lean_vault_loss_unrealized_get]
def lean_vault_loss_unrealized_get (v : Vault) : Option STNumber := v.lossUnrealized
@[export lean_vault_loss_unrealized_set]
def lean_vault_loss_unrealized_set (v : Vault) (lossUnrealized : Option STNumber) : Vault :=
  { v with lossUnrealized }

@[export lean_vault_share_mpt_id_get]
def lean_vault_share_mpt_id_get (v : Vault) : MPTID := v.shareMPTID
@[export lean_vault_share_mpt_id_set]
def lean_vault_share_mpt_id_set (v : Vault) (shareMPTID : MPTID) : Vault := { v with shareMPTID }

@[export lean_vault_withdrawal_policy_get]
def lean_vault_withdrawal_policy_get (v : Vault) : UInt8 := v.withdrawalPolicy
@[export lean_vault_withdrawal_policy_set]
def lean_vault_withdrawal_policy_set (v : Vault) (withdrawalPolicy : UInt8) : Vault :=
  { v with withdrawalPolicy }

@[export lean_vault_scale_get]
def lean_vault_scale_get (v : Vault) : UInt8 := v.scale
@[export lean_vault_scale_set]
def lean_vault_scale_set (v : Vault) (scale : UInt8) : Vault := { v with scale }

@[export lean_vault_associate_asset]
def lean_vault_associate_asset (v : Vault) (asset : Asset) (mode : UInt8) : Except String Vault :=
  v.associateAsset asset (decodeMode mode)

end XRPL.FFI
