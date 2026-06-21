import XRPL.Model.Protocol.LedgerEntries.DepositPreauth


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_deposit_preauth_empty]
def lean_deposit_preauth_empty (_ : Unit) : DepositPreauth := DepositPreauth.empty

@[export lean_deposit_preauth_key_get]
def lean_deposit_preauth_key_get (d : DepositPreauth) : UInt256 := d.key
@[export lean_deposit_preauth_key_set]
def lean_deposit_preauth_key_set (d : DepositPreauth) (key : UInt256) : DepositPreauth := { d with key }

@[export lean_deposit_preauth_flags_get]
def lean_deposit_preauth_flags_get (d : DepositPreauth) : UInt32 := d.flags
@[export lean_deposit_preauth_flags_set]
def lean_deposit_preauth_flags_set (d : DepositPreauth) (flags : UInt32) : DepositPreauth := { d with flags }

@[export lean_deposit_preauth_account_get]
def lean_deposit_preauth_account_get (d : DepositPreauth) : AccountID := d.account
@[export lean_deposit_preauth_account_set]
def lean_deposit_preauth_account_set (d : DepositPreauth) (account : AccountID) : DepositPreauth :=
  { d with account }

@[export lean_deposit_preauth_authorize_get]
def lean_deposit_preauth_authorize_get (d : DepositPreauth) : Option AccountID := d.authorize
@[export lean_deposit_preauth_authorize_set]
def lean_deposit_preauth_authorize_set (d : DepositPreauth) (authorize : Option AccountID) : DepositPreauth :=
  { d with authorize }

@[export lean_deposit_preauth_owner_node_get]
def lean_deposit_preauth_owner_node_get (d : DepositPreauth) : UInt64 := d.ownerNode
@[export lean_deposit_preauth_owner_node_set]
def lean_deposit_preauth_owner_node_set (d : DepositPreauth) (ownerNode : UInt64) : DepositPreauth :=
  { d with ownerNode }

@[export lean_deposit_preauth_previous_txn_id_get]
def lean_deposit_preauth_previous_txn_id_get (d : DepositPreauth) : UInt256 := d.previousTxnID
@[export lean_deposit_preauth_previous_txn_id_set]
def lean_deposit_preauth_previous_txn_id_set (d : DepositPreauth) (previousTxnID : UInt256) : DepositPreauth :=
  { d with previousTxnID }

@[export lean_deposit_preauth_previous_txn_lgr_seq_get]
def lean_deposit_preauth_previous_txn_lgr_seq_get (d : DepositPreauth) : UInt32 := d.previousTxnLgrSeq
@[export lean_deposit_preauth_previous_txn_lgr_seq_set]
def lean_deposit_preauth_previous_txn_lgr_seq_set (d : DepositPreauth) (previousTxnLgrSeq : UInt32) : DepositPreauth :=
  { d with previousTxnLgrSeq }

@[export lean_deposit_preauth_authorize_credentials_get]
def lean_deposit_preauth_authorize_credentials_get (d : DepositPreauth) : Option (List AcceptedCredential) :=
  d.authorizeCredentials
@[export lean_deposit_preauth_authorize_credentials_set]
def lean_deposit_preauth_authorize_credentials_set (d : DepositPreauth) (authorizeCredentials : Option (List AcceptedCredential)) : DepositPreauth :=
  { d with authorizeCredentials }

end XRPL.FFI
