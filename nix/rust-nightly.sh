#!@runtimeShell@
# Reaches the nightly Rust toolchain, which is deliberately kept off PATH.
# Packaged by nix/rust.nix, which explains why.

set -euo pipefail

usage() {
    echo "usage: rust-nightly (path | run <command>...)" >&2
    exit 2
}

case "${1-}" in
    path) printf '%s\n' "@rustNightlyBin@" ;;
    run)
        shift
        if [[ $# -eq 0 ]]; then
            usage
        fi
        export PATH="@rustNightlyBin@:${PATH}"
        exec "$@"
        ;;
    *) usage ;;
esac
