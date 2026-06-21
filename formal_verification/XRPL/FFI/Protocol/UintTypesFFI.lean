import XRPL.FFI.CommonFFI
import XRPL.Model.Basics.BaseUInt
import XRPL.Model.Protocol.Number
import XRPL.Model.Protocol.UintTypes


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_uint128_build]
def lean_uint128_build (bytes : ByteArray) : UInt128 := bytesToBitVec 128 bytes
@[export lean_uint128_bytes]
def lean_uint128_bytes (x : UInt128) : ByteArray := bitVecToBytes x

@[export lean_currency_build]
def lean_currency_build (bytes : ByteArray) : Currency := ⟨bytesToBitVec 160 bytes⟩
@[export lean_currency_bytes]
def lean_currency_bytes (c : Currency) : ByteArray := bitVecToBytes c.val

@[export lean_mpt_id_build]
def lean_mpt_id_build (bytes : ByteArray) : MPTID := ⟨bytesToBitVec 192 bytes⟩
@[export lean_mpt_id_bytes]
def lean_mpt_id_bytes (m : MPTID) : ByteArray := bitVecToBytes m.val

@[export lean_uint256_build]
def lean_uint256_build (bytes : ByteArray) : UInt256 := bytesToBitVec 256 bytes
@[export lean_uint256_bytes]
def lean_uint256_bytes (x : UInt256) : ByteArray := bitVecToBytes x

end XRPL.FFI
