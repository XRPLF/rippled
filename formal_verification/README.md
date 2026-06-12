# xrpl-lean4

Formal verification of XRPL in Lean 4 with Mathlib.

## Getting Started

### Prerequisites

- [elan](https://github.com/leanprover/elan) (Lean version manager)

### Setup

1. Clone the repository:

```bash
git clone <repo-url>
cd xrpl-lean4
```

2. Fetch Mathlib's pre-built cache (avoids rebuilding Mathlib from source):

```bash
lake exe cache get
```

3. Build the project:

```bash
lake build
```

`lake build` compiles all files and runs `#check` and `#print axioms` commands embedded in the source. Its output includes build progress and any axiom listings or warnings.

