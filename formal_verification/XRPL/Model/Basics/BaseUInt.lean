import Mathlib.Tactic


namespace XRPL.Model.Protocol

abbrev UInt256 := BitVec 256

def bytesToBitVec (w : Nat) (bytes : ByteArray) : BitVec w :=
  BitVec.ofNat w (bytes.foldl (fun acc b => acc * 256 + b.toNat) 0)

def natToBytesBE (len n : Nat) : ByteArray :=
  (List.range len).foldl
    (fun ba i => ba.push (UInt8.ofNat ((n >>> (8 * (len - 1 - i))) % 256))) ByteArray.empty

def bitVecToBytes {w : Nat} (x : BitVec w) : ByteArray := natToBytesBE (w / 8) x.toNat

end XRPL.Model.Protocol
