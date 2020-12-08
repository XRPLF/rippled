# Levelization

Levelization is the term used to describe efforts to prevent rippled from
having or creating cyclic dependencies.

rippled code is organized into directories under `src/rippled` (and
`src/test`) representing modules. The modules are intended to be
organized into "tiers" or "levels" such that a module from one level can
only include code from the same or lower levels, though including code
from the same level is not encouraged. Additionally, a module
in one level should never include code in an `impl` folder of any level
other than it's own.

Unfortunately, over time, enforcement of levelization has been
inconsistent, so the current state of the code doesn't necessarily
reflect these rules. Whenever possible, developers should refactor any
levelization violations they find (by moving files or individual
classes). At the very least, don't make things worse.

The table below summarizes the _desired_ division of modules.

| Level / Tier | Module(s)                                     |
|--------------|-----------------------------------------------|
| 01           | ripple/beast ripple/unity
| 02           | ripple/basics
| 03           | ripple/json ripple/crypto
| 04           | ripple/protocol
| 05           | ripple/core ripple/conditions ripple/consensus ripple/resource ripple/server
| 06           | ripple/peerfinder ripple/ledger ripple/nodestore ripple/net
| 07           | ripple/shamap ripple/overlay
| 08           | ripple/app
| 09           | ripple/rpc
| 10           | test/jtx test/unit_test test/beast test/csf
| 11           | test/crypto test/conditions test/json test/resource test/shamap test/peerfinder test/basics test/overlay
| 12           | test
| 13           | test/net test/protocol test/ledger test/consensus test/core test/server test/nodestore
| 14           | test/rpc test/app

(Note that `test` levelization is *much* less important and *much* less
strictly enforced than `ripple` levelization, other than the requirement
that `test` code should *never* be included in `ripple` code.)

## Validation

The [levelization.sh](levelization.sh) script can be run at any time on
a checked out repo, and will do an analysis of all the `#include`s in
the rippled source. It generates many files of [results](results):

* `rawincludes.txt`: The raw dump of the `#includes`
* `paths.txt`: A second dump grouping the source module
  to the destination module, deduped, and with frequency counts.
* `includes/`: A directory where each file represents a module and
  contains a list of modules and counts that the module _includes_.
* `includedby/`: Similar to `includes/`, but the other way around. Each
  file represents a module and contains a list of modules and counts
  that _include_ the module.
* [`loops.txt`](results/loops.txt): A list of direct loops detected
  between modules as they actually exist, as opposed to how they are
  desired as described above.
  This file is committed to the repo, and is used by the [levelization
  Github workflow](../../.github/workflows/levelization.yml) to validate
  that nothing changed.
* [`ordering.txt`](results/ordering.txt): A list showing relationships
  between modules where there are no loops as they actually exist, as
  opposed to how they are desired as described above.
  This file is committed to the repo, and is used by the [levelization
  Github workflow](../../.github/workflows/levelization.yml) to validate
  that nothing changed.
* [`levelization.yml`](../../.github/workflows/levelization.yml)
  Github Actions workflow to test that levelization loops haven't
  changed.  Unfortunately, if changes are detected, it can't tell if
  they are improvements or not, so if you have resolved any issues or
  done anything else to improve levelization, run `levelization.sh`,
  and commit the updated results.
