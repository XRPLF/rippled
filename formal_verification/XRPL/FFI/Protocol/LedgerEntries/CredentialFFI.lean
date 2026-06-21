import XRPL.Model.Protocol.LedgerEntries.Credential


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_credential_empty]
def lean_credential_empty (_ : Unit) : Credential := Credential.empty

@[export lean_credential_key_get]
def lean_credential_key_get (c : Credential) : UInt256 := c.key
@[export lean_credential_key_set]
def lean_credential_key_set (c : Credential) (key : UInt256) : Credential := { c with key }

@[export lean_credential_flags_get]
def lean_credential_flags_get (c : Credential) : UInt32 := c.flags
@[export lean_credential_flags_set]
def lean_credential_flags_set (c : Credential) (flags : UInt32) : Credential := { c with flags }

@[export lean_credential_subject_get]
def lean_credential_subject_get (c : Credential) : AccountID := c.subject
@[export lean_credential_subject_set]
def lean_credential_subject_set (c : Credential) (subject : AccountID) : Credential := { c with subject }

@[export lean_credential_issuer_get]
def lean_credential_issuer_get (c : Credential) : AccountID := c.issuer
@[export lean_credential_issuer_set]
def lean_credential_issuer_set (c : Credential) (issuer : AccountID) : Credential := { c with issuer }

@[export lean_credential_credential_type_get]
def lean_credential_credential_type_get (c : Credential) : ByteArray := ⟨c.credentialType.toArray⟩
@[export lean_credential_credential_type_set]
def lean_credential_credential_type_set (c : Credential) (credentialType : ByteArray) : Credential :=
  { c with credentialType := credentialType.toList }

@[export lean_credential_expiration_get]
def lean_credential_expiration_get (c : Credential) : Option UInt32 := c.expiration
@[export lean_credential_expiration_set]
def lean_credential_expiration_set (c : Credential) (expiration : Option UInt32) : Credential :=
  { c with expiration }

@[export lean_credential_uri_get]
def lean_credential_uri_get (c : Credential) : Option ByteArray := c.uri.map (⟨·.toArray⟩)
@[export lean_credential_uri_set]
def lean_credential_uri_set (c : Credential) (uri : Option ByteArray) : Credential :=
  { c with uri := uri.map (·.toList) }

@[export lean_credential_issuer_node_get]
def lean_credential_issuer_node_get (c : Credential) : UInt64 := c.issuerNode
@[export lean_credential_issuer_node_set]
def lean_credential_issuer_node_set (c : Credential) (issuerNode : UInt64) : Credential := { c with issuerNode }

@[export lean_credential_subject_node_get]
def lean_credential_subject_node_get (c : Credential) : Option UInt64 := c.subjectNode
@[export lean_credential_subject_node_set]
def lean_credential_subject_node_set (c : Credential) (subjectNode : Option UInt64) : Credential :=
  { c with subjectNode }

@[export lean_credential_previous_txn_id_get]
def lean_credential_previous_txn_id_get (c : Credential) : UInt256 := c.previousTxnID
@[export lean_credential_previous_txn_id_set]
def lean_credential_previous_txn_id_set (c : Credential) (previousTxnID : UInt256) : Credential :=
  { c with previousTxnID }

@[export lean_credential_previous_txn_lgr_seq_get]
def lean_credential_previous_txn_lgr_seq_get (c : Credential) : UInt32 := c.previousTxnLgrSeq
@[export lean_credential_previous_txn_lgr_seq_set]
def lean_credential_previous_txn_lgr_seq_set (c : Credential) (previousTxnLgrSeq : UInt32) : Credential :=
  { c with previousTxnLgrSeq }

end XRPL.FFI
