# Troubleshooting Nix problems

Common issues encountered when using the [Nix development shell](./nix.md), and
how to resolve them.

## Git worktrees

If `nix develop` fails with an error like:

```
error:
       … while fetching the input 'git+file:///path/to/rippled'

       error: opening Git repository "/path/to/rippled": unsupported extension name extensions.relativeworktrees (libgit2 error code = 6)
```

then your Nix is linked against a libgit2 older than **1.9.4**. Git 2.48+ writes
the `extensions.relativeWorktrees` config entry when a worktree is created with
relative paths (`git worktree add --relative-paths`, or with
`worktree.useRelativePaths=true`), and older libgit2 versions refuse to open a
repository that uses it. Nix uses libgit2 to read the flake, so evaluation
fails.

> [!IMPORTANT]
> This entry is written to the **shared** repository config, so once any
> relative worktree exists, `nix develop` fails in the main checkout too — not
> just inside the worktree.

### Workarounds

These work today, with any Nix version:

- bypass libgit2 with a `path:` flakeref: `nix develop "path:$PWD"`
  (note: this copies the working tree to the store and ignores `.gitignore`); or
- create worktrees with absolute paths (omit `--relative-paths`); or
- clear the extension if you don't need relative worktrees:
  `git config --unset extensions.relativeWorktrees`.

### Permanent fix

The fix is in [libgit2 1.9.4](https://github.com/libgit2/libgit2/releases/tag/v1.9.4),
so the real solution is a Nix that links against libgit2 `1.9.4` or newer. Check
which version yours links against:

```bash
nix-store -qR "$(readlink -f "$(command -v nix)")" | grep libgit2
```

> [!WARNING]
> `nix upgrade-nix` does **not** help yet. It installs the build from the
> official [`nix-fallback-paths`](https://github.com/NixOS/nixpkgs/blob/master/nixos/modules/installer/tools/nix-fallback-paths.nix),
> which is still linked against libgit2 `1.9.2` — there is no new upstream Nix
> release with the fix. (On some systems that build is even the exact store path
> you already have, making the upgrade a no-op.)

nixpkgs has already rebuilt Nix against the fixed libgit2 (e.g. `nix-2.34.7+1`),
so the cleanest path is to reinstall Nix using your usual installation method
once it picks up that rebuild, then re-run the `grep libgit2` check above to
confirm it reports `1.9.4` or newer.

Until then, prefer the workarounds above.
