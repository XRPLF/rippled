import Mathlib.Tactic


namespace XRPL.Model.Protocol

abbrev lsfDefaultRipple : UInt32 := 0x00800000
abbrev lsfMPTCanTransfer : UInt32 := 0x00000020
abbrev lsfRequireAuth : UInt32 := 0x00040000
abbrev lsfRequireDestTag : UInt32 := 0x00020000
abbrev lsfNoFreeze : UInt32 := 0x00200000
abbrev lsfGlobalFreeze : UInt32 := 0x00400000
abbrev lsfMPTLocked : UInt32 := 0x00000001
abbrev lsfMPTAuthorized : UInt32 := 0x00000002
abbrev lsfMPTRequireAuth : UInt32 := 0x00000004
abbrev lsfDepositAuth : UInt32 := 0x01000000
abbrev lsfAccepted : UInt32 := 0x00010000
abbrev lsfDisableMaster : UInt32 := 0x00100000
abbrev lsfVaultPrivate : UInt32 := 0x00010000

end XRPL.Model.Protocol
