import XRPL.Model.Ledger.Helpers.AccountRootHelpers
import XRPL.FFI.CommonFFI

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol
open XRPL.Model.Ledger (Ledger)
open XRPL.Model.Ledger.Helpers

def decodePseudoField (n : UInt8) : PseudoField := match n.toNat with
  | 0 => .vaultID | _ => .loanBrokerID

@[export lean_adjust_owner_count]
def lean_adjust_owner_count (ledger : Ledger) (sle : Option AccountRoot) (amount : Int32) : Ledger × Except String Unit :=
  match (adjustOwnerCount sle amount).run ledger with
  | .ok (_, ledger') => (ledger', .ok ())
  | .error e => (ledger, .error e)

@[export lean_check_destination_and_tag]
def lean_check_destination_and_tag (ledger : Ledger) (sleDst : Option AccountRoot)
    (hasDestinationTag : UInt8) : Ledger × Except String TER :=
  (ledger, .ok (checkDestinationAndTag sleDst (hasDestinationTag != 0)))

@[export lean_create_pseudo_account]
def lean_create_pseudo_account (ledger : Ledger) (pseudoOwnerKey : UInt256) (ownerField : UInt8)
    : Ledger × Except String (Except TER AccountRoot) :=
  match (createPseudoAccount pseudoOwnerKey (decodePseudoField ownerField)).run ledger with
  | .ok (res, ledger') => (ledger', .ok res)
  | .error e => (ledger, .error e)

@[export lean_is_pseudo_account]
def lean_is_pseudo_account (ledger : Ledger) (accountId : AccountID)
    : Ledger × Except String Bool :=
  (ledger, (isPseudoAccount accountId).run ledger)

@[export lean_xrp_liquid]
def lean_xrp_liquid (ledger : Ledger) (id : AccountID) (ownerCountAdj : Int32)
    : Ledger × Except String Int64 :=
  (ledger, ((xrpLiquid id ownerCountAdj).run ledger).map (·.value))

end XRPL.FFI
