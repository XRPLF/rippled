import XRPL.FFI.CommonFFI
import XRPL.Model.Protocol.AccountID


namespace XRPL.FFI

open XRPL.Model.Protocol (AccountID bitVecToBytes)

@[export lean_account_id_build]
def lean_account_id_build (bytes : ByteArray) : AccountID := AccountID.fromRaw bytes
@[export lean_account_id_bytes]
def lean_account_id_bytes (a : AccountID) : ByteArray := bitVecToBytes a.val

end XRPL.FFI
