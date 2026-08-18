# Prototype results

These results were recorded on August 14, 2026. They show that the local
workflow operates end to end; they are not an estimate of production accuracy.

See [README.md](README.md) for setup and replay commands and
[DESIGN.md](DESIGN.md) for the architecture.

## Static routing

The corpus has five must-route cases, one invariant-authorization case, and one
high-fanout clean control. With a budget of six review clusters:

| Route class             | Within budget | Interpretation                                                |
| ----------------------- | ------------: | ------------------------------------------------------------- |
| Must-route              |           5/5 | Every expected cluster ranked within the budget.              |
| Invariant authorization |           1/1 | The changed `OverrideFreeze` rule ranked first.               |
| High-fanout ranking     |           0/1 | The expected candidate remained available at rank 138 of 246. |

The high-fanout case is a behavior-preserving Payment and Sponsor change in a
generic fork with 243 consumers and no Payment-specific signal. Its result is a
ranking limitation, not candidate loss.

## Live model checks

Three final calls used AWS Bedrock model `anthropic.claude-opus-5` at `xhigh`.
Each matched its predefined behavior, coverage, and primary-location result.
The prompt later received a wording-only cleanup; these calls were not rerun.

| Case                                      | Routing and localization                            | Result                                    | Turns | Output tokens |
| ----------------------------------------- | --------------------------------------------------- | ----------------------------------------- | ----: | ------------: |
| Historical Batch delegate-consent bypass  | Cluster rank 1; five locations; `checkSign` primary | Bug found; regression test found          |    18 |        17,860 |
| Historical AMM freeze-override regression | Invariant route rank 1; `OverrideFreeze` primary    | Bug found; regression test found          |    12 |         8,373 |
| Clean delegation and sponsor refactor     | Cluster rank 1; `checkSponsor` primary              | Behavior correct; combination tests found |    19 |        19,506 |

Together the calls used 49 turns, 45,739 output tokens, 98 input tokens,
1,395,737 cache-read tokens, and 164,484 cache-write tokens. There were no
infrastructure errors.

The AMM case was routed through the changed `OverrideFreeze` authorization rule
and its `AMMClawback` holder. The model identified the DeepFreeze/Cleanup
interaction while reading source. It is therefore recorded as an invariant
route, not an exact static feature-pair hit.

The first clean-control call judged behavior correctly but did not inspect the
tests, so coverage remained `unclear`. We then changed the general prompt to
search for likely combination tests early. The retry found four relevant cases
in `Sponsor_test` and returned `covered`. Because this adjustment followed a
visible result, the retry is not independent evidence.

## Recommended demo

Use the historical Batch delegate-consent replay. It changes one authorization
line so a delegated inner transaction requires `sfAccount` instead of
`sfDelegate`. Inner transactions bypass the normal signature path, so the
change breaks the delegate-consent boundary.

The selector ranked the boundary first and grouped five shared locations. The
model ruled out the fee-payer path, identified `checkSign` as the primary
control point, described the reachable failure modes, and found the existing
regression tests. This case shows why clustering related locations is more
useful than reviewing each static intersection independently.

Use `run_corpus.py --mode run --with-model --case
historical_batch_delegate_consent` as documented in [README.md](README.md).
Expected results remain outside the model-readable worktree.

## Validation completed

The recorded prototype passed the full Python test suite. The C++ suite was not
run for these results. Corpus worktrees are recreated from the commits and
count-checked mutations in `examples/corpus/manifest.json`.

## Limitations

- Two known bugs and one clean change are enough for a demonstration, not an
  accuracy claim.
- The cases are visible, the prompt was changed after one clean-control miss,
  and there were no repeated stochastic trials.
- Some supporting model text was broader than its citations. Findings still
  require reviewer judgment.
- Latency and token use are high. The design is currently a focused second
  opinion for a small number of selected boundaries.
- The corpus has only two clean controls and does not measure calibration,
  semantic citation quality, or graph-assisted review against a bare-diff
  baseline.
- Two replay revisions were created in temporary worktrees. The manifest can
  recreate them, but durable replay commits would simplify long-term retention.
- The demonstrations inspected source and tests without running C++ suites.
