# check-tools snapshots

These files capture the output of [`bin/check-tools.sh`](../../bin/check-tools.sh)
— the versions of the development tooling — in each Nix environment:

| File                  | Environment                         |
| --------------------- | ----------------------------------- |
| `nix-nixos-amd64.txt` | `nix-nixos` CI image, `linux/amd64` |
| `nix-nixos-arm64.txt` | `nix-nixos` CI image, `linux/arm64` |
| `macos.txt`           | macOS, inside `nix develop`         |

The [`check-tools`](../../.github/workflows/check-tools.yml) workflow regenerates
each snapshot in its environment and fails if it differs from the committed file.
So if you change the environment (bump the image tag in
[`linux.json`](../../.github/scripts/strategy-matrix/linux.json), update
`flake.lock`, change the tool list in `check-tools.sh`, …) you must regenerate
and commit the affected snapshots.

Each snapshot is `check-tools.sh` stdout with the git-clone connectivity check
skipped (`CHECK_TOOLS_SKIP_CLONE=1`) and any preceding shell-hook output trimmed
(`sed -n '/^Detected OS:/,$p'`), so it contains only deterministic version data.

## Regenerating

The two Linux snapshots come from the `nix-nixos` image (Docker or a compatible
runtime such as Apple `container`). The image tag is pinned in `linux.json`:

```bash
img="ghcr.io/xrplf/xrpld/nix-nixos:$(jq -r .image_tag .github/scripts/strategy-matrix/linux.json)"

for arch in amd64 arm64; do
    container run --rm -i -e CHECK_TOOLS_SKIP_CLONE=1 -a "${arch}" --entrypoint bash "${img}" -s \
        <bin/check-tools.sh |
        sed -n '/^Detected OS:/,$p' \
            >"nix/check-tools/nix-nixos-${arch}.txt"
done
```

(With Docker, replace `container run … -a "${arch}"` with
`docker run … --platform "linux/${arch}"`.)

The macOS snapshot is generated locally:

```bash
nix develop -c bash -c 'CHECK_TOOLS_SKIP_CLONE=1 bash bin/check-tools.sh' |
    sed -n '/^Detected OS:/,$p' \
        >nix/check-tools/macos.txt
```
