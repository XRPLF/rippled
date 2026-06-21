import XRPL.Model.Protocol.AcceptedCredential

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_accepted_credential_build]
def lean_accepted_credential_build (issuer : AccountID) (credentialType : ByteArray) : AcceptedCredential :=
  { issuer, credentialType := credentialType.toList }
@[export lean_accepted_credential_issuer_get]
def lean_accepted_credential_issuer_get (ac : AcceptedCredential) : AccountID := ac.issuer
@[export lean_accepted_credential_type_get]
def lean_accepted_credential_type_get (ac : AcceptedCredential) : ByteArray := ⟨ac.credentialType.toArray⟩

@[export lean_accepted_credential_list_empty]
def lean_accepted_credential_list_empty (_ : Unit) : List AcceptedCredential := []
@[export lean_accepted_credential_list_append]
def lean_accepted_credential_list_append (list : List AcceptedCredential) (item : AcceptedCredential) : List AcceptedCredential :=
  list ++ [item]

end XRPL.FFI
