import XRPL.Model.Protocol.Rules


namespace XRPL.FFI

open XRPL.Model.Protocol

@[export lean_rules_empty]
def lean_rules_empty (_ : Unit) : Rules := Rules.empty

@[export lean_rules_all]
def lean_rules_all (_ : Unit) : Rules := Rules.all

@[export lean_rules_enable]
def lean_rules_enable (r : Rules) (code : UInt8) : Rules :=
  match Amendment.ofCode code with
  | some a => r.enable a
  | none   => r

end XRPL.FFI
