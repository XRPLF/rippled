namespace XRPL.Model.Protocol

-- The test oracle enables every `Supported::Yes` amendment, so the test default is `Rules.all`.
inductive Amendment where
  | singleAssetVault
  | permissionedDomains
  | lendingProtocol
  | permissionDelegationV1_1
  | credentials
  | mpTokensV1
  | deepFreeze
  | fixCleanup3_1_3
  | fixCleanup3_2_0
  deriving DecidableEq, Repr, BEq

def Amendment.all : List Amendment :=
  [ .singleAssetVault, .permissionedDomains, .lendingProtocol, .permissionDelegationV1_1,
    .credentials, .mpTokensV1, .deepFreeze, .fixCleanup3_1_3, .fixCleanup3_2_0 ]

-- Stable FFI codes so C++ can build a `Rules` value
def Amendment.toCode : Amendment → UInt8
  | .singleAssetVault         => 0
  | .permissionedDomains      => 1
  | .lendingProtocol          => 2
  | .permissionDelegationV1_1 => 3
  | .credentials              => 4
  | .mpTokensV1               => 5
  | .deepFreeze               => 6
  | .fixCleanup3_1_3          => 7
  | .fixCleanup3_2_0          => 8

def Amendment.ofCode : UInt8 → Option Amendment
  | 0 => some .singleAssetVault
  | 1 => some .permissionedDomains
  | 2 => some .lendingProtocol
  | 3 => some .permissionDelegationV1_1
  | 4 => some .credentials
  | 5 => some .mpTokensV1
  | 6 => some .deepFreeze
  | 7 => some .fixCleanup3_1_3
  | 8 => some .fixCleanup3_2_0
  | _ => none

structure Rules where
  amendments : List Amendment := []

def Rules.enabled (r : Rules) (a : Amendment) : Bool := r.amendments.contains a
def Rules.empty : Rules := {}
def Rules.all : Rules := { amendments := Amendment.all }
def Rules.enable (r : Rules) (a : Amendment) : Rules := { r with amendments := a :: r.amendments }

end XRPL.Model.Protocol
