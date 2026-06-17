ARG BASE_IMAGE=nixos/nix:latest

# Nix builder
FROM nixos/nix:latest AS builder-source

RUN mkdir -p ~/.config/nix && \
    echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf

# Copy our source and setup our working dir.
COPY nix/ci-env.nix /tmp/build/nix/ci-env.nix
COPY nix/packages.nix /tmp/build/nix/packages.nix
COPY nix/utils.nix /tmp/build/nix/utils.nix
COPY flake.nix /tmp/build/
COPY flake.lock /tmp/build/
WORKDIR /tmp/build

FROM builder-source AS builder

# Build our Nix CI environment (all build tools in a single store path)
RUN nix \
    --option filter-syscalls false \
    build

# Copy the Nix store closure into a directory. The Nix store closure is the
# entire set of Nix store values that we need for our build.
RUN mkdir /tmp/nix-store-closure && \
    cp -R $(nix-store -qR result/) /tmp/nix-store-closure

# Final image
FROM ${BASE_IMAGE} AS final

ARG BASE_IMAGE

# bash is not located at /bin/bash in nixos/nix, so we need to create a symlink to it.
RUN if echo "${BASE_IMAGE}" | grep -qiE 'nixos'; then \
        ln -s /root/.nix-profile/bin/bash /bin/bash; \
    fi

# Use Bash as the default shell for RUN commands, using the options
# `set -o errexit -o pipefail`, and as the entrypoint.
SHELL ["/bin/bash", "-e", "-o", "pipefail", "-c"]
ENTRYPOINT ["/bin/bash"]

# Copy /nix/store and the env symlink tree
COPY --from=builder /tmp/nix-store-closure /nix/store
COPY --from=builder /tmp/build/result /nix/ci-env

ENV PATH="/nix/ci-env/bin:${PATH}"

# Point HTTPS clients (git, curl, conan, ...) at the CA bundle shipped in the
# Nix CI environment, so TLS verification works without ca-certificates being
# installed in the system.
ENV SSL_CERT_FILE="/nix/ci-env/etc/ssl/certs/ca-bundle.crt"
ENV GIT_SSL_CAINFO="/nix/ci-env/etc/ssl/certs/ca-bundle.crt"

# Externally-built dynamically-linked ELF binaries hard-code the loader path
# (e.g. /lib64/ld-linux-x86-64.so.2) in their PT_INTERP header. Install it
# from the Nix store when the base image doesn't already provide one.
COPY nix/docker/loader-path.sh /tmp/loader-path.sh

RUN <<EOF
target="$(/tmp/loader-path.sh)"

if [ ! -e "${target}" ]; then
    # Use the loader from the same glibc that gcc links libc against, so
    # ld-linux and libc/libpthread share GLIBC_PRIVATE symbols at runtime.
    src="$(dirname "$(gcc -print-file-name=libc.so.6)")/$(basename "${target}")"
    [ -e "${src}" ] || { echo "ld-linux not found at ${src}" >&2; exit 1; }
    mkdir -p "$(dirname "${target}")"
    cp "${src}" "${target}"
fi
EOF

COPY bin/check-tools.sh /tmp/check-tools.sh
RUN /tmp/check-tools.sh

# Sanity-check that the g++/clang++ are able to build binaries, including sanitizer-instrumented ones.
COPY nix/docker/test_files/cpp_sources/ /tmp/cpp_sources/
COPY nix/docker/test_files/compile-cpp-sources.sh /tmp/compile-cpp-sources.sh
RUN /tmp/compile-cpp-sources.sh /tmp/cpp_sources /tmp/bins

# Tester: start from a clean BASE_IMAGE, install sanitizer runtime libraries,
# and run the compiled test binaries to verify they execute correctly.
FROM ${BASE_IMAGE} AS tester

ARG BASE_IMAGE

# bash is not located at /bin/bash in nixos/nix, so we need to create a symlink to it.
RUN if echo "${BASE_IMAGE}" | grep -qiE 'nixos'; then \
        ln -s /root/.nix-profile/bin/bash /bin/bash; \
    fi

SHELL ["/bin/bash", "-e", "-o", "pipefail", "-c"]

# Sanity-check that the built binaries run correctly in the vanilla base image, with the necessary sanitizer runtime libraries installed.
COPY bin/install-sanitizer-libs.sh /tmp/install-sanitizer-libs.sh
COPY nix/docker/test_files/run-test-binaries.sh /tmp/run-test-binaries.sh
COPY --from=final /tmp/bins /tmp/bins

RUN <<EOF
if echo "${BASE_IMAGE}" | grep -qiE 'nixos'; then
    echo "Skipping runnning binaries on NixOS."
else
    /tmp/install-sanitizer-libs.sh
    /tmp/run-test-binaries.sh /tmp/bins
fi
touch /tmp/tests-passed
EOF

# Output: the final image, gated on a successful test run in the tester stage.
# Copying the sentinel from tester creates a hard build dependency: if the test
# run above failed, this stage — and the overall build — fails too.
FROM final
COPY --from=tester /tmp/tests-passed /tmp/tests-passed
