import XRPL.Model.tx.Transactor


namespace XRPL.Model.tx

/-! ## ApplyContext — mutable apply phase (mirrors C++ `tx/ApplyContext.h`)

The ledger lives in the `ApplyView` (StateT) monad; the context carries the tx. -/

structure ApplyContext (α : Type) [Tx α] where
  tx : α

end XRPL.Model.tx
