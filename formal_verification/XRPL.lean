/-
Entry point of the whole Lean package: models, proofs and FFI exports. `lake build` starts
here and checks everything the three imports reach. New files never go here directly, so add
them to Model.lean, Properties.lean or FFI.lean instead.
-/
import XRPL.FFI.FFI
import XRPL.Model.Model
import XRPL.Properties.Properties
