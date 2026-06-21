import XRPL.Model.Ledger.Helpers.CredentialHelpers

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol
open XRPL.Model.Ledger (Ledger)
open XRPL.Model.Ledger.Helpers (deleteSLE authorizedDepositPreauth verifyDepositPreauth)

@[export lean_credentials_delete_sle]
def lean_credentials_delete_sle (ledger : Ledger) (sleCredential : Option Credential) : Ledger × Except String TER :=
  match (deleteSLE sleCredential).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

@[export lean_credentials_authorized_deposit_preauth]
def lean_credentials_authorized_deposit_preauth (ledger : Ledger) (credIDs : STVector256) (dst : AccountID) : Ledger × Except String TER :=
  (ledger, (authorizedDepositPreauth credIDs dst).run ledger)

@[export lean_credentials_verify_deposit_preauth]
def lean_credentials_verify_deposit_preauth (ledger : Ledger) (credentialIDs : Option STVector256) (src dst : AccountID)
    (sleDst : Option AccountRoot) : Ledger × Except String TER :=
  match (verifyDepositPreauth credentialIDs src dst sleDst).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

end XRPL.FFI
