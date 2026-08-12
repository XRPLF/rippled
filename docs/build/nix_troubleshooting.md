# Troubleshooting Nix problems

Common issues encountered when using the [Nix development shell](./nix.md), and
how to resolve them.

## `command not found: nix` after a macOS update

If a shell suddenly can't find `nix` at all:

```
$ nix develop
zsh: command not found: nix
```

then Nix is almost certainly still installed — only the shell hook that puts it
on your `PATH` is gone. Confirm that first:

```bash
ls -l /nix/var/nix/profiles/default/bin/nix
```

If that exists, the installation is fine and this is purely a `PATH` problem.

### Why it happens

The installer does not touch your dotfiles. Instead it sources a setup script
from the Nix store by editing **system-wide** rc files:

| Shell | File the installer edits              |
| ----- | ------------------------------------- |
| bash  | `/etc/bashrc`, `/etc/bash.bashrc`     |
| zsh   | `/etc/zshrc`                          |
| fish  | `$__fish_sysconf_dir/conf.d/nix.fish` |

macOS manages `/etc/zshrc`, so an OS update can replace it with the vendor copy
and silently drop the Nix block. `/etc/bashrc` and the fish file usually survive,
which is why the breakage often shows up in zsh only. You can verify this by
diffing against the backup the installer left behind:

```bash
diff /etc/zshrc /etc/zshrc.backup-before-nix
```

If they are identical, the Nix snippet was wiped. This is upstream issue
[NixOS/nix#3616](https://github.com/NixOS/nix/issues/3616).

### Fix

To unblock the current shell:

```bash
. /nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh
```

For a permanent fix, add the snippet to your **user** rc file rather than
restoring `/etc/zshrc` — user dotfiles are not clobbered by OS updates:

```bash
cat >>~/.zshrc <<'EOF'

# Nix
if [ -e '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh' ]; then
  . '/nix/var/nix/profiles/default/etc/profile.d/nix-daemon.sh'
fi
# End Nix
EOF
```

The scripts guard against double-sourcing via `__ETC_PROFILE_NIX_SOURCED`, so
this is safe even if a system-wide hook is later restored.

> [!NOTE]
> `/etc/zshrc` and `~/.zshrc` are only read by **interactive** zsh. If the
> snippet is present but `zsh -c '…'`, a script, or an IDE terminal still can't
> find `nix`, that shell is non-interactive — put the snippet in `~/.zshenv`
> instead.

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
