#!/bin/sh

set -eux

export LC_ALL=C

# Print relevant CI environment to allow reproducing the job outside of CI.
print_environment() {
    # Turn off -x because it messes up the output
    set +x
    # There are many ways to print variable names and their content. This one
    # does not rely on bash.
    for var in WERROR_CFLAGS MAKEFLAGS BUILD \
            ECMULTWINDOW ECMULTGENPRECISION ASM WIDEMUL WITH_VALGRIND EXTRAFLAGS \
            EXPERIMENTAL ECDH RECOVERY SCHNORRSIG \
            SECP256K1_TEST_ITERS BENCH SECP256K1_BENCH_ITERS CTIMETESTS\
            EXAMPLES \
            HOST WRAPPER_CMD \
            CC CFLAGS CPPFLAGS AR NM
    do
        eval "isset=\${$var+x}"
        if [ -n "$isset" ]; then
            eval "val=\${$var}"
            # shellcheck disable=SC2154
            printf '%s="%s" ' "$var" "$val"
        fi
    done
    echo "$0"
    set -x
}
print_environment

# Start persistent wineserver if necessary.
# This speeds up jobs with many invocations of wine (e.g., ./configure with MSVC) tremendously.
case "$WRAPPER_CMD" in
    *wine*)
        # Make sure to shutdown wineserver whenever we exit.
        trap "wineserver -k || true" EXIT INT HUP
        # This is apparently only reliable when we run a dummy command such as "hh.exe" afterwards.
        wineserver -p && wine hh.exe
        ;;
esac

env >> test_env.log

if [ -n "${CC+x}" ]; then
    # The MSVC compiler "cl" doesn't understand "-v"
    $CC -v || true
fi
if [ "$WITH_VALGRIND" = "yes" ]; then
    valgrind --version
fi
if [ -n "$WRAPPER_CMD" ]; then
    $WRAPPER_CMD --version
fi

./autogen.sh

./configure \
    --enable-experimental="$EXPERIMENTAL" \
    --with-test-override-wide-multiply="$WIDEMUL" --with-asm="$ASM" \
    --with-ecmult-window="$ECMULTWINDOW" \
    --with-ecmult-gen-precision="$ECMULTGENPRECISION" \
    --enable-module-ecdh="$ECDH" --enable-module-recovery="$RECOVERY" \
    --enable-module-schnorrsig="$SCHNORRSIG" \
    --enable-examples="$EXAMPLES" \
    --enable-ctime-tests="$CTIMETESTS" \
    --with-valgrind="$WITH_VALGRIND" \
    --host="$HOST" $EXTRAFLAGS

# We have set "-j<n>" in MAKEFLAGS.
make

# Print information about binaries so that we can see that the architecture is correct
file *tests* || true
file bench* || true
file .libs/* || true

# This tells `make check` to wrap test invocations.
export LOG_COMPILER="$WRAPPER_CMD"

make "$BUILD"

# Using the local `libtool` because on macOS the system's libtool has nothing to do with GNU libtool
EXEC='./libtool --mode=execute'
if [ -n "$WRAPPER_CMD" ]
then
    EXEC="$EXEC $WRAPPER_CMD"
fi

if [ "$BENCH" = "yes" ]
then
    {
        $EXEC ./bench_ecmult
        $EXEC ./bench_internal
        $EXEC ./bench
    } >> bench.log 2>&1
fi

if [ "$CTIMETESTS" = "yes" ]
then
    if [ "$WITH_VALGRIND" = "yes" ]; then
        ./libtool --mode=execute valgrind --error-exitcode=42 ./ctime_tests > ctime_tests.log 2>&1
    else
        $EXEC ./ctime_tests > ctime_tests.log 2>&1
    fi
fi

# Rebuild precomputed files (if not cross-compiling).
if [ -z "$HOST" ]
then
    make clean-precomp clean-testvectors
    make precomp testvectors
fi

# Check that no repo files have been modified by the build.
# (This fails for example if the precomp files need to be updated in the repo.)
git diff --exit-code
