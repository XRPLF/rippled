import XRPL.Model.Protocol.LedgerEntries.PermissionedDomain

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_permissioned_domain_empty]
def lean_permissioned_domain_empty (_ : Unit) : PermissionedDomain := PermissionedDomain.empty

@[export lean_permissioned_domain_key_get]
def lean_permissioned_domain_key_get (p : PermissionedDomain) : UInt256 := p.key
@[export lean_permissioned_domain_key_set]
def lean_permissioned_domain_key_set (p : PermissionedDomain) (key : UInt256) : PermissionedDomain := { p with key }

@[export lean_permissioned_domain_flags_get]
def lean_permissioned_domain_flags_get (p : PermissionedDomain) : UInt32 := p.flags
@[export lean_permissioned_domain_flags_set]
def lean_permissioned_domain_flags_set (p : PermissionedDomain) (flags : UInt32) : PermissionedDomain := { p with flags }

@[export lean_permissioned_domain_owner_get]
def lean_permissioned_domain_owner_get (p : PermissionedDomain) : AccountID := p.owner
@[export lean_permissioned_domain_owner_set]
def lean_permissioned_domain_owner_set (p : PermissionedDomain) (owner : AccountID) : PermissionedDomain := { p with owner }

@[export lean_permissioned_domain_sequence_get]
def lean_permissioned_domain_sequence_get (p : PermissionedDomain) : UInt32 := p.sequence
@[export lean_permissioned_domain_sequence_set]
def lean_permissioned_domain_sequence_set (p : PermissionedDomain) (sequence : UInt32) : PermissionedDomain := { p with sequence }

@[export lean_permissioned_domain_accepted_credentials_get]
def lean_permissioned_domain_accepted_credentials_get (p : PermissionedDomain) : List AcceptedCredential := p.acceptedCredentials
@[export lean_permissioned_domain_accepted_credentials_set]
def lean_permissioned_domain_accepted_credentials_set (p : PermissionedDomain) (acceptedCredentials : List AcceptedCredential) : PermissionedDomain :=
  { p with acceptedCredentials }

@[export lean_permissioned_domain_owner_node_get]
def lean_permissioned_domain_owner_node_get (p : PermissionedDomain) : UInt64 := p.ownerNode
@[export lean_permissioned_domain_owner_node_set]
def lean_permissioned_domain_owner_node_set (p : PermissionedDomain) (ownerNode : UInt64) : PermissionedDomain :=
  { p with ownerNode }

@[export lean_permissioned_domain_previous_txn_id_get]
def lean_permissioned_domain_previous_txn_id_get (p : PermissionedDomain) : UInt256 := p.previousTxnID
@[export lean_permissioned_domain_previous_txn_id_set]
def lean_permissioned_domain_previous_txn_id_set (p : PermissionedDomain) (previousTxnID : UInt256) : PermissionedDomain :=
  { p with previousTxnID }

@[export lean_permissioned_domain_previous_txn_lgr_seq_get]
def lean_permissioned_domain_previous_txn_lgr_seq_get (p : PermissionedDomain) : UInt32 := p.previousTxnLgrSeq
@[export lean_permissioned_domain_previous_txn_lgr_seq_set]
def lean_permissioned_domain_previous_txn_lgr_seq_set (p : PermissionedDomain) (previousTxnLgrSeq : UInt32) : PermissionedDomain :=
  { p with previousTxnLgrSeq }

end XRPL.FFI
