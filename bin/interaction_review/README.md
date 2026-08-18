# Interaction review

`interaction_review` finds feature boundaries affected by a `rippled` change and,
optionally, asks a model to review the relevant code and tests. Static analysis
chooses what to inspect. The model decides whether behavior is broken and whether
the feature combination is tested. The PR comment contains only the model's
findings; detailed routing data stays in the JSON artifacts.

For implementation choices, see [DESIGN.md](DESIGN.md). For measured prototype
results, see [PROTOTYPE_RESULTS.md](PROTOTYPE_RESULTS.md).

## Setup

From this directory:

```bash
python -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

Graph extraction requires:

- a CMake compile database at repo-root `.build/compile_commands.json` or
  `build/compile_commands.json`, generated with
  `CMAKE_EXPORT_COMPILE_COMMANDS=ON`; and
- a native libclang library. The default is
  `/opt/homebrew/opt/llvm/lib/libclang.dylib`; use `--libclang` to override it.

Install the separate model dependency only when live judging is needed:

```bash
pip install -r requirements-judge.txt
```

## Local workflow

Run commands from `bin/interaction_review`.

```bash
# 1. Build the feature graph and interaction set.
python build_graph.py --out out/

# 2. Map a change to graph nodes.
python pr_map.py --base develop --head HEAD

# 3. Rank and retain review candidates.
python select_interactions.py

# 4. Inspect prompts without making a model call.
python judge_interactions.py --selected out/selected.json --dry-run

# 5. Or run the live Bedrock review.
python judge_interactions.py --aws-region us-east-1

# 6. Render the advisory comment.
python render_comment.py --out out/comment.md
```

Omit `--head HEAD` in step 2 to review tracked and untracked worktree changes.
The default file locations under `out/` are:

| Artifact            | Purpose                                                 |
| ------------------- | ------------------------------------------------------- |
| `graph.json`        | Features, shared resources, edges, and source locations |
| `interactions.json` | Feature relationships through those resources           |
| `touched.json`      | Graph nodes and files affected by the diff              |
| `selected.json`     | Ranked candidates and grouped investigation inputs      |
| `judged.json`       | Model verdicts, citations, usage, and evidence audit    |
| `comment.md`        | Concise, model-only PR output                           |

All structured artifacts are schema-validated. The renderer ignores a stale
`judged.json` when it does not match the current `selected.json`.

## What the result means

The model reports behavior and test coverage separately:

| Behavior               | Coverage  | Display result |
| ---------------------- | --------- | -------------- |
| `broken`               | any value | `gap`          |
| `correct` or `unclear` | `missing` | `coverage_gap` |
| `correct`              | `covered` | `handled`      |
| otherwise              | otherwise | `unclear`      |

`unclear` is not a clean bill of health. It means the available evidence did
not support a stronger conclusion. Static scores control which investigation is
reviewed; they are not evidence and do not appear in the PR comment.

## Evaluate an existing run

`evaluate_run.py` compares saved artifacts with an expected-results file. It
does not call the model or change the run.

```bash
python evaluate_run.py \
    --selected out/selected.json \
    --judged out/judged.json \
    --oracle path/to/oracle.json \
    --budget 6 \
    --markdown-out out/evaluation.md \
    --json-out out/evaluation.json \
    --fail-on-miss
```

Omit `--judged` to evaluate selection only. `run_corpus.py` creates an
expected-results file for each replay automatically.

Primary routing metrics use feature-pair investigation clusters. The report
keeps the earlier resource-row metrics under `legacy_resource_rows` for
comparison and never mixes them with cluster recall or rank. An invariant case
may use `match_mode=invariant_route` when the graph finds an authorization rule
and the model must identify the wider semantic interaction from source.

## Replay the local corpus

The corpus contains five historical reverse mutations and two clean controls.
See [examples/corpus/README.md](examples/corpus/README.md) for case definitions
and interpretation.

```bash
# Validate the manifest and print the execution plan. No worktree is created.
.venv/bin/python run_corpus.py

# Run the static pipeline and evaluation in disposable worktrees.
.venv/bin/python run_corpus.py --mode run --budget 6

# Make a live model call for one case. Both flags are required.
AWS_REGION=us-east-1 .venv/bin/python run_corpus.py \
    --mode run --with-model --case historical_batch_delegate_consent
```

Each case writes its artifacts under `<run-root>/artifacts/<case>/`. The matrix
also writes `corpus-run.json` and JSON/Markdown summaries. By default the run
root is a new temporary directory and worktrees are removed when complete. A
specified `--run-root` must be outside the repository. Use `--keep-worktrees`
only for debugging.

The generated expected-results file stays outside the model-readable worktree.
The runner never switches or resets the shared checkout.

## Model access and safety

The judge uses Amazon Bedrock through `AnthropicBedrockMantle`; model IDs use the
`anthropic.` prefix. Its tools are read-only and limited to tracked files under
`src/`, `include/`, `test/`, and `tests/`. It cannot read Git metadata, `cfg/`,
credentials, this tool's implementation, previous output, corpus definitions,
or expected results.

The selected base and head are bound to an immutable source snapshot. For a
worktree run, the tracked diff and checkout commit are fingerprinted and checked
throughout the conversation. A conclusive behavior or coverage result requires
a `file:line` citation that resolves in the snapshot and was shown to the model;
unsupported dimensions are changed to `unclear`.

Conversations and tool output are bounded, but there is no hard per-run cost,
wall-clock, or total token limit. Missing credentials and model failures leave
the cluster unjudged and do not block the static artifacts.

## CI

CI uses two workflows:

- `interaction-review.yml` checks out PR code, builds the graph, selects work,
  optionally runs the model, renders the comment, and uploads artifacts. It has
  no repository write permission.
- `interaction-review-comment.yml` runs on `workflow_run`, downloads the
  artifact, and updates the marked PR comment. It never checks out PR code.

A `/interaction-review` PR comment forces analysis when the author has push
access. Do not give this forced-run path AWS credentials for untrusted fork
heads until judging runs with base-pinned code and dependencies in a separate
privileged job. The current analysis job configures, builds, and installs code
from the PR before model credentials are available, so its AWS role must be
narrowly scoped.

Judging is enabled when the repository defines `INTERACTION_REVIEW_AWS_ROLE`
and `INTERACTION_REVIEW_AWS_REGION`; `INTERACTION_REVIEW_MODEL` is optional.
The Mantle endpoint signs requests as `bedrock-mantle`, not the classic
`bedrock` service.

## Maintained configuration

Four small files describe relationships the extractor cannot infer safely:

- `config/field_to_amendment.yml`: common transaction field to amendment or
  `core`;
- `config/flag_to_amendment.yml`: transaction flag to amendment;
- `config/gate_allowlist.yml`: amendment globals allowed to remain unresolved;
- `config/transactor_impl_overrides.yml`: implementation paths that cannot be
  derived automatically.

Extraction fails on missing mappings, inactive amendment names, unresolved
gates, missing implementations, invalid source spans, and schema errors. These
failures are intentional: a new protocol lever should require an explicit graph
decision.

## Tests

```bash
python -m pytest -q -p no:cacheprovider tests
```

Tests that require libclang or a compile database skip when those dependencies
are unavailable. This command does not run the rippled C++ test suite.

## Current coverage limits

- Fork extraction scans definitions reachable from the `Transactor.cpp`
  translation unit and repository helper headers, not the full codebase.
- Payment-path internals, some ledger helpers, and invariants without a mapped
  privilege bit are not represented as resource families.
- Test files are searched by the model but are not graph nodes, so static
  mapping alone cannot determine whether a PR adds a boundary test.
- Changes outside a node's source span are reported as context and are not
  attributed to that node.

These are graph-model limits, not evidence that an unreported change is safe.
