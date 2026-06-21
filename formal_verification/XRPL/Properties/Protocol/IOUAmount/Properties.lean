import XRPL.Properties.Protocol.IOUAmount.Common.Defs
import XRPL.Properties.Protocol.IOUAmount.Accessors.Accessors
import XRPL.Properties.Protocol.IOUAmount.Signum.Signum
import XRPL.Properties.Protocol.IOUAmount.Constructors.Constructors
import XRPL.Properties.Protocol.IOUAmount.ToNumber.ToNumber
import XRPL.Properties.Protocol.IOUAmount.Compare.Compare
import XRPL.Properties.Protocol.IOUAmount.Neg.Neg
import XRPL.Properties.Protocol.IOUAmount.Add.RoundsWithin
import XRPL.Properties.Protocol.IOUAmount.Sub.RoundsWithin

/-! # `IOUAmount` operator-correctness aggregator

Mirrors `Number/Properties.lean`: imports every `IOUAmount` headline file. `IOUAmount`
is a `(mantissa, exponent)` decimal float that rounds through the `Number` layer. Equality
is structural and ordering decides the rational order (`Compare`), `operator_neg` is exact
(`Neg`), and `operator_add` / `operator_sub` carry per-mode rounding bounds
(`Add`/`Sub`, `RoundsWithin`). The shared predicates (`ToNumberExact`, `εDirected`) and
`toRat` bridges live in `Common/`. `mulRatio` is not yet covered. -/
