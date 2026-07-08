#!/usr/bin/env bash
# Compare SHA-512-half performance: OpenSSL 3.5.6 vs 3.6.2.
# Binaries are identical builds (GCC 13.3, Release, same locked deps),
# differing only in the statically linked OpenSSL.
set -euo pipefail

cd "$(dirname "$0")/.build"

OLD=./xrpl_tests-openssl-3.5.6
NEW=./xrpl_tests-openssl-3.6.2

# Single hash of the full 100 KB buffer. Startup-dominated (~ms), kept for
# parity with earlier result logs; expect near-identical numbers.
FILTER=OpenSSL.SingleHashFullSlice
hyperfine -N \
    --warmup 5 \
    --min-runs 200 \
    --command-name "openssl-3.5.6 singlehash" "$OLD --gtest_filter=$FILTER" \
    --command-name "openssl-3.6.2 singlehash" "$NEW --gtest_filter=$FILTER" \
    --export-markdown openssl_comparison_singlehash.md \
    --export-json openssl_comparison_singlehash.json

# 100k hashes per run; the meaningful benchmark.
FILTER=OpenSSL.MultihashAllSlices
hyperfine \
    --warmup 5 \
    --min-runs 20 \
    --command-name "openssl-3.5.6 multihash" "$OLD --gtest_filter=$FILTER" \
    --command-name "openssl-3.6.2 multihash" "$NEW --gtest_filter=$FILTER" \
    --export-markdown openssl_comparison.md \
    --export-json openssl_comparison.json

echo
echo "Results saved to .build/openssl_comparison*.md and .build/openssl_comparison*.json"
