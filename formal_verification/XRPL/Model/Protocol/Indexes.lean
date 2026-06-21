import XRPL.Model.Protocol.AcceptedCredential
import XRPL.Model.Protocol.AccountID
import XRPL.Model.Protocol.Digest
import XRPL.Model.Protocol.Keylet
import XRPL.Model.Protocol.UintTypes
import XRPL.Model.Basics.Blob

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.Model.Protocol

inductive LedgerNameSpace where
  | account | trustLine | mptIssuance | mpToken | credential
  | depositPreauth | depositPreauthCreds | vault | loanBroker | loan
  | permissionedDomain
  deriving DecidableEq, Repr

def LedgerNameSpace.toUInt16 : LedgerNameSpace → UInt16
  | .account             => 'a'.toNat.toUInt16
  | .trustLine           => 'r'.toNat.toUInt16
  | .mptIssuance         => '~'.toNat.toUInt16
  | .mpToken             => 't'.toNat.toUInt16
  | .credential          => 'D'.toNat.toUInt16
  | .depositPreauth      => 'p'.toNat.toUInt16
  | .depositPreauthCreds => 'P'.toNat.toUInt16
  | .vault               => 'V'.toNat.toUInt16
  | .loanBroker          => 'l'.toNat.toUInt16
  | .loan                => 'L'.toNat.toUInt16
  | .permissionedDomain  => 'm'.toNat.toUInt16

def serU16 (x : UInt16) : ByteArray := natToBytesBE 2 x.toNat
def serU32 (x : UInt32) : ByteArray := natToBytesBE 4 x.toNat
def serU64 (x : UInt64) : ByteArray := natToBytesBE 8 x.toNat
def serUInt256 (x : UInt256) : ByteArray := bitVecToBytes x
def serAccountID (id : AccountID) : ByteArray := bitVecToBytes id.val
def serCurrency (c : Currency) : ByteArray := bitVecToBytes c.val
def serMPTID (m : MPTID) : ByteArray := bitVecToBytes m.val
def serBlob (b : Blob) : ByteArray := ⟨b.toArray⟩

def indexHash (space : LedgerNameSpace) (payload : ByteArray) : UInt256 :=
  sha512Half (serU16 space.toUInt16 ++ payload)

-- Lexicographic byte order for blobs, matching std::lexicographical_compare.
def blobLt : Blob → Blob → Bool
  | [], [] => false
  | [], _ :: _ => true
  | _ :: _, [] => false
  | a :: as, b :: bs => if a == b then blobLt as bs else a < b

-- Order on {issuer, credentialType} matching std::set<std::pair<AccountID, Slice>>.
def credLe (a b : AcceptedCredential) : Bool :=
  if a.issuer == b.issuer then !blobLt b.credentialType a.credentialType else a.issuer.lt b.issuer

def Keylet.account (id : AccountID) : Keylet :=
  ⟨.accountRoot, indexHash .account (serAccountID id)⟩

def Keylet.credential (subject issuer : AccountID) (credType : Blob) : Keylet :=
  ⟨.credential, indexHash .credential (serAccountID subject ++ serAccountID issuer ++ serBlob credType)⟩

def Keylet.depositPreauthAccount (owner authorized : AccountID) : Keylet :=
  ⟨.depositPreauth, indexHash .depositPreauth (serAccountID owner ++ serAccountID authorized)⟩

def Keylet.depositPreauthCreds (owner : AccountID) (authCreds : List AcceptedCredential) : Keylet :=
  -- rippled hashes a std::set (sorted + unique); dedupe to match it byte-for-byte.
  let sorted := authCreds.dedup.mergeSort credLe
  let hashes := sorted.map (fun ac => sha512Half (serAccountID ac.issuer ++ serBlob ac.credentialType))
  let hashesBytes := hashes.foldl (fun acc h => acc ++ serUInt256 h) ByteArray.empty
  let count := serU64 hashes.length.toUInt64
  let body := serAccountID owner ++ hashesBytes ++ count
  ⟨.depositPreauth, indexHash .depositPreauthCreds body⟩

-- Trust line: accounts are canonicalised (min, max) before hashing.
def Keylet.line (id0 id1 : AccountID) (currency : Currency) : Keylet :=
  let lo := id0.min id1
  let hi := id0.max id1
  ⟨.rippleState, indexHash .trustLine (serAccountID lo ++ serAccountID hi ++ serCurrency currency)⟩

def Keylet.loan (loanBrokerID : UInt256) (loanSeq : UInt32) : Keylet :=
  ⟨.loan, indexHash .loan (serUInt256 loanBrokerID ++ serU32 loanSeq)⟩

def Keylet.loanBroker (owner : AccountID) (seq : UInt32) : Keylet :=
  ⟨.loanBroker, indexHash .loanBroker (serAccountID owner ++ serU32 seq)⟩

def Keylet.mptIssuance (mptID : MPTID) : Keylet :=
  ⟨.mptokenIssuance, indexHash .mptIssuance (serMPTID mptID)⟩

def Keylet.mptoken (mptID : MPTID) (holder : AccountID) : Keylet :=
  let issuanceKey := (Keylet.mptIssuance mptID).key
  ⟨.mptoken, indexHash .mpToken (serUInt256 issuanceKey ++ serAccountID holder)⟩

def Keylet.permissionedDomainFromSeq (owner : AccountID) (seq : UInt32) : Keylet :=
  ⟨.permissionedDomain, indexHash .permissionedDomain (serAccountID owner ++ serU32 seq)⟩

def Keylet.permissionedDomain (domainID : UInt256) : Keylet :=
  ⟨.permissionedDomain, domainID⟩

def Keylet.vault (owner : AccountID) (seq : UInt32) : Keylet :=
  ⟨.vault, indexHash .vault (serAccountID owner ++ serU32 seq)⟩

end XRPL.Model.Protocol
