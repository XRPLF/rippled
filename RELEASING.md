# Branching and Release Management

This document describes how we branch, release, and merge releases back into `develop`.
It does not define version naming
(e.g., what constitutes a major-minor, patch, or beta release).

Examples use `X.Y` for a release line:
`X` and `Y` are placeholders, while the trailing `x` is literal,
e.g., `release/X.Y.x` is `release/3.4.x` for the `3.4` line.

## Branches

| Branch          | Purpose                                                                                   |
| :-------------- | :---------------------------------------------------------------------------------------- |
| `develop`       | Main development branch. Betas and the first RC of a major-minor release are tagged here. |
| `staging/X.Y.x` | Where fixes for the `X.Y` line are developed and RCs are prepared.                        |
| `release/X.Y.x` | The last public RC or release of the `X.Y` line.                                          |

A release line is named `X.Y.x`
because one branch serves every patch release of that line (`X.Y.0`, `X.Y.1`, `X.Y.2`, ...).
The `/` groups branches hierarchically,
so tools can filter them and protection rules can target `release/*` and `staging/*`.

## Principles

- **Releases are merged back into `develop`, never cherry-picked or rebased onto it.**
  Cherry-picked and rebased commits get new hashes,
  so Git can't tell that `develop` already has them.
  Future merges then replay them and produce artificial conflicts,
  and it's hard to verify that every fix actually reached `develop`.
  Merging keeps a single history:
  Git knows exactly which release commits `develop` contains,
  and no fix is left behind.
- **Branches only move forward.**
  `develop`, `staging/X.Y.x`, and `release/X.Y.x` are never rewritten.
- **Cherry-picking only goes from `develop` to a staging branch**,
  for fixes that must get into a release after the code freeze
  (see [Emergency Fixes From `develop`](#emergency-fixes-from-develop)).
- **Security fixes are prepared privately and published with the release that contains them**,
  so vulnerabilities are not disclosed prematurely.

## Release Lifecycle

This diagram shows a major-minor release and its first patch release.
The staging and release branches are drawn as one line.

```text
develop                            staging/X.Y.x & release/X.Y.x
   │
   ├── Tag: X.Y.0-b1
   ├── Tag: X.Y.0-bN
   ├── Tag: X.Y.0-rc1 ───────────────┐ (branches created)
   │                                 │
   │  (development continues)        ├── Fixes
   │                                 ├── Tag: X.Y.0-rcN
   │                                 ├── Tag: X.Y.0 (final)
   ◀──── (merge) ────────────────────┤
   │                                 ├── Fixes
   │                                 ├── Tag: X.Y.1-rcN
   │                                 ├── Tag: X.Y.1 (final)
   ◀──── (merge) ────────────────────┤
   │
   ▼
```

### Betas, First RC & Branching

> [!NOTE]
> This phase applies only to a new major-minor release (e.g., `X.Y.0`).
> Patch releases work on the existing branches of the line.

1. **Betas:** All beta versions (e.g., `X.Y.0-b1`) are built and tagged directly on `develop`.
2. **First RC:** We release the first RC (`X.Y.0-rc1`)
   once everything that should be included in the release has been merged.
3. **Branches:** `staging/X.Y.x` and `release/X.Y.x` are created from `develop`
   at the commit tagged `X.Y.0-rc1`.
   The first RC also kicks off the QE process.
4. **Code freeze:** No new features or unrelated changes are pulled from `develop`
   into `staging/X.Y.x` or `release/X.Y.x`.
   Only critical stabilization fixes go into the line.
5. **No large changes on `develop`:** Until `X.Y.0` is [merged back](#merging-back-into-develop),
   large changes (e.g., big refactors, moving or renaming many files, mass reformatting)
   are not merged into `develop`,
   so that fixes on the line and the merge back don't run into conflicts.

### Release Candidates

1. Fixes are developed against `staging/X.Y.x`.
2. When ready, a new RC is created on `staging/X.Y.x`,
   and `release/X.Y.x` is fast-forwarded to it.

RCs that contain unpublished security fixes are not published,
and don't touch the public branches.

RCs are not merged back into `develop`:
`X.Y.0-rc1` is tagged on `develop` itself,
and all later changes reach `develop` with the [final release](#final-release).

### Final Release

1. Unpublished security fixes, if any, are merged into `staging/X.Y.x`.
2. `release/X.Y.x` is fast-forwarded to `staging/X.Y.x`,
   and the release is tagged on it.
3. The release is immediately [merged back into `develop`](#merging-back-into-develop).
   For a major-minor release, this lifts the freeze on large changes in `develop`.

### Merging Back Into `develop`

The merge back is a regular PR into `develop`
whose branch contains a real merge commit of the release tag:

1. Create a branch from `develop`, run `git merge <tag>`, and resolve any conflicts.
2. Once the PR is approved, `develop` is fast-forwarded to the PR branch,
   so the merge commit lands as it is.
   Never squash or rebase it.
   Never add it to the merge queue either: the queue squashes PRs, which would drop the merge commit.
3. If `develop` moves while the PR is open, redo the merge on top of the new `develop`.

After the merge, `git log develop..<tag>` must be empty.

## Special Cases

### Emergency Fixes From `develop`

If a commit was merged to `develop`
and needs to be included in a release after the code freeze:

1. Create a PR that cherry-picks the commit onto `staging/X.Y.x`.
2. Leave `develop` as it is, with no reverts.
3. Follow the [release candidates](#release-candidates) process as usual.

When the release is later merged back into `develop`,
both sides already contain the same change,
so the merge usually resolves it cleanly or with a trivial conflict.
From Git's perspective, the cherry-picked commit then becomes part of `develop` too.

### Several Supported Lines

When a fix must ship in more than one supported line (e.g., `X.Y` and `X.(Y+1)`),
the lines are merged upwards rather than cherry-picked between:

1. The fix goes into the oldest line first.
2. After that line's release,
   its `release/X.Y.x` is merged into the staging branch of the next newer line,
   and so on up to the newest line.
3. The newest line is merged back into `develop` as usual.

This way every newer line, and eventually `develop`, contains the history of the older lines.
