import XRPL.FFI.CommonFFI
import XRPL.Model.Ledger.Helpers.TokenHelpers

namespace XRPL.FFI

open XRPL.Model.Protocol
open XRPL.Model.Ledger (Ledger)
open XRPL.Model.Ledger.Helpers

def decodeFreezeHandling (n : UInt8) : FreezeHandling := match n.toNat with
  | 0 => .fhIGNORE_FREEZE | _ => .fhZERO_IF_FROZEN

def decodeAuthHandling (n : UInt8) : AuthHandling := match n.toNat with
  | 0 => .ahIGNORE_AUTH | _ => .ahZERO_IF_UNAUTHORIZED

def decodeSpendableHandling (n : UInt8) : SpendableHandling := match n.toNat with
  | 0 => .shSIMPLE_BALANCE | _ => .shFULL_BALANCE

-- rippled `WaiveTransferFee { No = 0, Yes = 1 }`; the Lean type declares
-- `yes` before `no`, so decode by C++ value, not by constructor position.
def decodeWaiveTransferFee (n : UInt8) : WaiveTransferFee := match n.toNat with
  | 0 => .no | _ => .yes

def decodeWaiveMPTCanTransfer (n : UInt8) : WaiveMPTCanTransfer := match n.toNat with
  | 0 => .no | _ => .yes

def decodeAllowMPTOverflow (n : UInt8) : AllowMPTOverflow := match n.toNat with
  | 0 => .no | _ => .yes

-- rippled `AuthType { StrongAuth = 0, WeakAuth = 1, Legacy = 2 }`.
def decodeAuthType (n : UInt8) : AuthType := match n.toNat with
  | 0 => .strongAuth | 1 => .weakAuth | _ => .legacy

-- Return the run's `Except` as-is so a thrown error (a computation failure)
@[export lean_can_add_holding]
def lean_can_add_holding (ledger : Ledger) (asset : Asset) : Ledger × Except String TER :=
  (ledger, (canAddHoldingAsset asset).run ledger)

@[export lean_add_empty_holding]
def lean_add_empty_holding (ledger : Ledger) (accountID : AccountID) (priorBalance : Int64)
    (asset : Asset) : Ledger × Except String TER :=
  match (addEmptyHoldingAsset accountID ⟨priorBalance⟩ asset).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

@[export lean_is_global_frozen]
def lean_is_global_frozen (ledger : Ledger) (asset : Asset) : Ledger × Except String Bool :=
  (ledger, (isGlobalFrozen asset).run ledger)

@[export lean_is_vault_pseudo_account_frozen]
def lean_is_vault_pseudo_account_frozen (ledger : Ledger) (account : AccountID)
    (mptShare : MPTIssue) (depth : UInt8) : Ledger × Except String Bool :=
  (ledger, (isVaultPseudoAccountFrozen depth.toNat account mptShare).run ledger)

@[export lean_check_frozen]
def lean_check_frozen (ledger : Ledger) (account : AccountID) (asset : Asset) : Ledger × Except String TER :=
  (ledger, (checkFrozen account asset).run ledger)

@[export lean_check_deep_frozen]
def lean_check_deep_frozen (ledger : Ledger) (account : AccountID) (asset : Asset) : Ledger × Except String TER :=
  (ledger, (checkDeepFrozen account asset).run ledger)

@[export lean_can_transfer]
def lean_can_transfer (ledger : Ledger) (asset : Asset) (from_ to_ : AccountID) (waive : UInt8)
    : Ledger × Except String TER :=
  (ledger, (canTransfer asset from_ to_ (decodeWaiveMPTCanTransfer waive)).run ledger)

@[export lean_require_auth]
def lean_require_auth (ledger : Ledger) (asset : Asset) (account : AccountID) (authType : UInt8)
    : Ledger × Except String TER :=
  (ledger, (requireAuth asset account (decodeAuthType authType)).run ledger)

@[export lean_account_holds]
def lean_account_holds (ledger : Ledger) (account : AccountID) (asset : Asset)
    (zeroIfFrozen zeroIfUnauthorized mode includeFullBalance : UInt8)
    : Ledger × Except String STAmount :=
  (ledger, (accountHolds account asset (decodeFreezeHandling zeroIfFrozen)
    (decodeAuthHandling zeroIfUnauthorized) (decodeMode mode)
    (decodeSpendableHandling includeFullBalance)).run ledger)

@[export lean_account_send]
def lean_account_send (ledger : Ledger) (uSenderID uReceiverID : AccountID) (saAmount : STAmount)
    (mode waiveFee allowOverflow : UInt8) : Ledger × Except String TER :=
  match (accountSend uSenderID uReceiverID saAmount (decodeMode mode)
      (decodeWaiveTransferFee waiveFee) (decodeAllowMPTOverflow allowOverflow)).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

@[export lean_account_send_multi]
def lean_account_send_multi (ledger : Ledger) (senderID : AccountID) (asset : Asset)
    (receivers : MultiplePaymentDestinations) (mode waiveFee : UInt8) : Ledger × Except String TER :=
  match (accountSendMulti senderID asset receivers (decodeMode mode)
      (decodeWaiveTransferFee waiveFee)).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

@[export lean_remove_empty_holding]
def lean_remove_empty_holding (ledger : Ledger) (accountID : AccountID) (asset : Asset)
    : Ledger × Except String TER :=
  match (removeEmptyHoldingAsset accountID asset).run ledger with
  | .ok (ter, ledger') => (ledger', .ok ter)
  | .error e => (ledger, .error e)

end XRPL.FFI
