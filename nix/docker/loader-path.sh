#!/bin/bash

case "$(uname -m)" in
    x86_64) LOADER=/lib64/ld-linux-x86-64.so.2 ;;
    aarch64) LOADER=/lib/ld-linux-aarch64.so.1 ;;
    *)
        echo "Unsupported arch: $(uname -m)" >&2
        exit 1
        ;;
esac

echo "${LOADER}"
