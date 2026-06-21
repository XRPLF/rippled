import XRPL.Model.Protocol.Fees

set_option linter.style.longLine false
set_option linter.style.emptyLine false

namespace XRPL.FFI

open XRPL.Model.Protocol (Fees)

@[export lean_fees_build]
def lean_fees_build (base reserve increment : Int64) : Fees :=
  ⟨⟨base⟩, ⟨reserve⟩, ⟨increment⟩⟩
@[export lean_fees_base]
def lean_fees_base (f : Fees) : Int64 := f.base.value
@[export lean_fees_reserve]
def lean_fees_reserve (f : Fees) : Int64 := f.reserve.value
@[export lean_fees_increment]
def lean_fees_increment (f : Fees) : Int64 := f.increment.value

@[export lean_fees_account_reserve]
def lean_fees_account_reserve (f : Fees) (ownerCount : UInt32) : Int64 :=
  (f.accountReserve ownerCount).value

end XRPL.FFI
