import XRPL.FFI.CommonFFI
import XRPL.Model.tx.ConcreteTx

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol
open XRPL.Model.tx

@[export lean_vault_set_tx_build]
def lean_vault_set_tx_build
    (txID : UInt256) (account : AccountID) (fee : Int64) (sequence flags : UInt32)
    (vaultID : UInt256) (data : Option ByteArray) (assetsMaximum : Option STNumber)
    (domainID : Option UInt256) : ConcreteTx :=
  .vaultSet
    { txID, account, sequence, flags, vaultID, assetsMaximum, domainID,
      fee := ⟨fee⟩, data := data.map (·.toList) }

end XRPL.FFI
