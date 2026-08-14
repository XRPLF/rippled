# Replay corpus

This corpus turns real `rippled` fixes into repeatable, source-only bug replays.
It checks two separate questions:

1. Did static analysis route the change to the correct feature boundary within
   the review budget?
2. Given that boundary, did the model reach the expected behavior and coverage
   result from valid source evidence?

[`manifest.json`](manifest.json) is the machine-readable case definition.
`tests/test_corpus.py` validates its commits, paths, mutation anchors, feature
IDs, resources, and expected results against the local Git object database.

## Cases

| Case                            | Boundary                               | Primary resource           | Expected result  |
| ------------------------------- | -------------------------------------- | -------------------------- | ---------------- |
| Batch delegate consent          | `Batch` × `PermissionDelegationV1_1`   | `checkSign`                | Broken; covered  |
| Batch inner fee sponsor         | `Batch` × `Sponsor`                    | `isFeeSponsored`           | Broken; covered  |
| TrustSet sponsor routing        | `TrustSet` × `Sponsor`                 | `isReserveSponsored`       | Broken; covered  |
| EscrowFinish reserve recycle    | `EscrowFinish` × `Sponsor`             | `isReserveSponsored`       | Broken; covered  |
| AMM freeze override             | `AMMClawback` × `DeepFreeze`           | `OverrideFreeze` invariant | Broken; covered  |
| Named fee predicate control     | `Payment` × `Sponsor`                  | `getFeePayer`              | Correct; covered |
| Named sponsor predicate control | `PermissionDelegationV1_1` × `Sponsor` | `checkSponsor`             | Correct; covered |

Cases have one of three routing classes:

- `must_route`: the graph should place the exact cluster within budget;
- `stretch_route`: the graph should find an authorization rule, after which the
  model may discover wider context from source; or
- `ambiguity_stress`: the correct candidate must remain available, but a generic
  high-fanout change may rank below the review budget.

The AMM case uses `match_mode=invariant_route`. It measures retrieval of the
`OverrideFreeze` rule and its `AMMClawback` holder, not exact static recall of an
AMMClawback and DeepFreeze pair. The Payment and Sponsor control is the
high-fanout stress case.

## Run the corpus

From `bin/interaction_review`:

```bash
# Validate every case and print the execution plan without creating worktrees.
.venv/bin/python run_corpus.py

# Plan one case or only the clean controls.
.venv/bin/python run_corpus.py --case historical_batch_delegate_consent
.venv/bin/python run_corpus.py --kind synthetic_control

# Run static routing and evaluation in disposable worktrees.
.venv/bin/python run_corpus.py --mode run --budget 6

# Make a live Bedrock call only with both explicit opt-ins.
AWS_REGION=us-east-1 .venv/bin/python run_corpus.py \
    --mode run --with-model --case historical_batch_delegate_consent
```

The runner creates a detached worktree per case, applies an exact count-checked
production mutation, retains the fixed regression tests, and removes the
worktree afterward. It never switches or resets the shared checkout and rejects
mutation targets under `cfg/` or `.git/`.

By default artifacts go to a new temporary directory. A supplied `--run-root`
must be outside the repository. Each case writes selected, judged, rendered,
and evaluated artifacts under `<run-root>/artifacts/<case>/`; the matrix writes
JSON and Markdown summaries. Use `--keep-worktrees` only for debugging.

## Case construction rules

For a historical bug, the fixed tree is the PR base and a source-only reverse
mutation is the head. The mutation must change only the named production
behavior and keep the regression test. Do not review `buggy_commit..fix_commit`:
that asks the model to inspect a correct fix and cannot measure behavior and
coverage independently.

For a clean control, apply only the semantics-preserving change recorded in the
manifest. Do not include unrelated hunks from the seed commit.

Each case also records a current-develop anchor. This allows the runner to check
that the mutation still applies while the historical fix commit remains the
stable reproduction source.

## Scoring and isolation

Report routing separately from model judgment:

- cluster Recall@K and primary-resource presence;
- behavior and coverage agreement;
- primary and supporting location accuracy;
- citation validity; and
- model turns and tokens.

Keep `must_route`, `stretch_route`, and `ambiguity_stress` results separate, and
run clean controls with bugs. Otherwise a model that labels every selected
boundary as broken can look successful.

The manifest and generated expected-results file are evaluator inputs, never
model inputs. They remain outside the model-readable source snapshot. Do not
copy bug descriptions, mutation instructions, expected verdicts, or test
assertions into the prompt.

For a quick metadata-only check:

```bash
.venv/bin/pytest -q tests/test_corpus.py
```

This check reads local Git objects only. It does not change branches, build or
run C++, or call a model.
