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

## `wint_t` / `uint32_t` errors from the Nix libc++ headers

A build that mixes the Nix toolchain with the system SDK fails in libc++ itself,
with errors that look nothing like your code:

```
/nix/store/...-libcxx-.../include/c++/v1/cwchar:136:9: error: target of using declaration conflicts with declaration already in scope
  136 | using ::wint_t _LIBCPP_USING_IF_EXISTS;
/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/sys/_types/_wint_t.h:32:25: note: target of using declaration
...
error: use of undeclared identifier 'UINT32_C'
```

The give-away is the second path: Nix's libc++ headers are being combined with
the **Xcode Command Line Tools** SDK instead of the Nix one.

### Why it happens

`SDKROOT` and `DEVELOPER_DIR` are what point the toolchain at the Nix SDK, and
they are not baked into the compiler — a dev shell gets them from the
`apple-sdk` setup hook. CMake, finding neither, asks `xcrun`, which answers with
the system SDK. Nix's `libc++` and Apple's headers then declare the same types
twice.

### Fix

Run the build from inside the dev shell (`nix develop`), or from an environment
that exports both variables. To confirm which SDK a configured build is using:

```bash
grep -o '\-isysroot [^ ]*' build/compile_commands.json | sort -u
```

It should print a `/nix/store/...-apple-sdk-*` path. If it prints
`/Library/Developer/CommandLineTools/...`, re-configure from within the shell —
CMake caches the sysroot, so an existing `build/` directory keeps the wrong one.

## `Library not loaded: /nix/store/…` from a binary that used to work

A binary stops starting after a `nix flake update`, or after
`nix-collect-garbage` removes the paths the previous toolchain used:

```
dyld[57271]: Library not loaded: /nix/store/…-libresolv-93/lib/libresolv.9.dylib
```

[`bin/check-nix-store-refs.sh`](../../bin/check-nix-store-refs.sh) finds the same
thing without having to run anything, and points at the file:

```
$ bin/check-nix-store-refs.sh ~/.conan2-nix
/Users/you/.conan2-nix/p/b/c-are…/p/bin/adig
    /nix/store/…-libresolv-93/lib/libresolv.9.dylib
Checked 135 files in /Users/you/.conan2-nix (2495 skipped), 1 of them reference the Nix store.
```

### Why it happens

The binary records a store path that no longer exists. Nothing we build should:
see [Prebuilt packages](./nix.md#prebuilt-packages) for why, and
`libresolvSystemStub` in [`nix/darwin.nix`](../../nix/darwin.nix) for the one
dependency that needed help to comply.

A Conan package ID does not encode the nixpkgs revision, so a package built
before that stub existed stays in your local cache and keeps being reused. The
dev shell is also what tends to produce one: it is a slightly _less_ isolated
build environment than CI's, because `mkShell` puts every tool's headers and
libraries on the compiler's search path — which is how c-ares found the Nix
`libresolv` in the first place.

### Fix

Drop the package the check named and let Conan refetch or rebuild it:

```bash
conan remove 'c-ares/*'
```
