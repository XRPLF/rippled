import json
import os
import re
from pathlib import Path

from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.env import Environment

from conan import ConanFile
from conan.errors import ConanException

DEV_VERSION = "0.0.0-dev"


class Xrpl(ConanFile):
    name = "xrpl"

    license = "ISC"
    author = "John Freeman <jfreeman@ripple.com>"
    url = "https://github.com/xrplf/rippled"
    description = "The XRP Ledger"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "assertions": [True, False],
        "benchmark": [True, False],
        "coverage": [True, False],
        "fPIC": [True, False],
        "formal_verification": [True, False],
        "jemalloc": [True, False],
        "rocksdb": [True, False],
        "shared": [True, False],
        "static": [True, False],
        "tests": [True, False],
        "unity": [True, False],
        "xrpld": [True, False],
    }

    requires = [
        "corrosion/0.6.1",
        "ed25519/2015.03",
        "fast_float/8.2.10",
        "grpc/1.81.1",
        "libarchive/3.8.7",
        "nudb/2.0.9",
        "openssl/3.6.3",
        "soci/4.0.3",
        "xrpl-rpc-spec/0.1.21",
        "zlib/1.3.2",
    ]

    test_requires = [
        "gtest/1.17.0",
    ]

    tool_requires = [
        "grpc/<host_version>",
        "protobuf/<host_version>",
    ]

    default_options = {
        "assertions": False,
        "benchmark": True,
        "coverage": False,
        "fPIC": True,
        "formal_verification": False,
        "jemalloc": False,
        "rocksdb": True,
        "shared": False,
        "static": True,
        "tests": False,
        "unity": False,
        "xrpld": False,
        "boost/*:without_cobalt": True,
        "boost/*:without_context": False,
        "boost/*:without_coroutine": True,
        "boost/*:without_coroutine2": False,
        "date/*:header_only": True,
        "ed25519/*:shared": False,
        "grpc/*:shared": False,
        "grpc/*:secure": True,
        "grpc/*:codegen": True,
        "grpc/*:cpp_plugin": True,
        "grpc/*:csharp_ext": False,
        "grpc/*:csharp_plugin": False,
        "grpc/*:node_plugin": False,
        "grpc/*:objective_c_plugin": False,
        "grpc/*:php_plugin": False,
        "grpc/*:python_plugin": False,
        "grpc/*:ruby_plugin": False,
        "grpc/*:otel_plugin": False,
        "libarchive/*:shared": False,
        "libarchive/*:with_acl": False,
        "libarchive/*:with_bzip2": False,
        "libarchive/*:with_cng": False,
        "libarchive/*:with_expat": False,
        "libarchive/*:with_iconv": False,
        "libarchive/*:with_libxml2": False,
        "libarchive/*:with_lz4": True,
        "libarchive/*:with_lzma": False,
        "libarchive/*:with_lzo": False,
        "libarchive/*:with_nettle": False,
        "libarchive/*:with_openssl": False,
        "libarchive/*:with_pcreposix": False,
        "libarchive/*:with_xattr": False,
        "libarchive/*:with_zlib": False,
        "lz4/*:shared": False,
        "openssl/*:no_dtls": True,
        "openssl/*:no_ssl": True,
        "openssl/*:no_ssl3": True,
        "openssl/*:no_tls1": True,
        "openssl/*:no_tls1_1": True,
        "openssl/*:shared": False,
        "openssl/*:tls_security_level": 2,
        "protobuf/*:shared": False,
        "protobuf/*:with_zlib": True,
        "rocksdb/*:enable_sse": False,
        "rocksdb/*:lite": False,
        "rocksdb/*:shared": False,
        "rocksdb/*:use_rtti": True,
        "rocksdb/*:with_jemalloc": False,
        "rocksdb/*:with_lz4": True,
        "rocksdb/*:with_snappy": True,
        "secp256k1/*:shared": False,
        "snappy/*:shared": False,
        "soci/*:shared": False,
        "soci/*:with_sqlite3": True,
        "soci/*:with_boost": True,
        "xrpl-rpc-spec/*:server": "xrpld",
        "xxhash/*:shared": False,
    }

    # default_options only reach the host context;
    # give tool_requires (and their dependencies) the same dependency options.
    default_build_options = {k: v for k, v in default_options.items() if "/" in k}

    def set_version(self):
        self.version = self.version or DEV_VERSION

    def configure(self):
        if self.settings.compiler == "apple-clang":
            self.options["boost"].visibility = "global"
        if self.settings.compiler in ["clang", "gcc"]:
            self.options["boost"].without_cobalt = True

    def _lean_version(self):
        # formal_verification/lean-toolchain pins "leanprover/lean4:vX.Y.Z".
        path = os.path.join(self.recipe_folder, "formal_verification", "lean-toolchain")
        with open(path, encoding="utf-8") as f:
            return f.read().strip().split(":v")[1]

    def _assert_lake_closure(self, package_manifest):
        # lean4-deps' prebuilt oleans are only usable if lake resolves the same
        # dependency closure they were built from. Otherwise lake re-resolves and
        # re-elaborates all of mathlib -- hours, with no error. A lean-toolchain
        # bump is caught by the version pin; this covers a `lake update` within
        # the same version.
        def pins(manifest):
            text = Path(manifest).read_text(encoding="utf-8")
            return {p["name"]: p["rev"] for p in json.loads(text)["packages"]}

        ours = pins(
            Path(self.recipe_folder, "formal_verification", "lake-manifest.json")
        )
        theirs = pins(package_manifest)
        if ours != theirs:
            drifted = [
                f"{name}: ours {ours.get(name)}, lean4-deps {theirs.get(name)}"
                for name in sorted(ours.keys() | theirs.keys())
                if ours.get(name) != theirs.get(name)
            ]
            raise ConanException(
                "formal_verification/lake-manifest.json pins a different Lean "
                "dependency closure than lean4-deps was built against:\n  "
                + "\n  ".join(drifted)
                + "\nRepublish lean4-deps from this manifest, or restore the pins."
            )

    def requirements(self):
        if self.options.benchmark:
            self.requires("benchmark/1.9.5")
        self.requires("boost/1.91.0", force=True, transitive_headers=True)
        self.requires("date/3.0.4", transitive_headers=True)
        if self.options.formal_verification:
            self.requires(f"lean4/{self._lean_version()}", transitive_headers=True)
            self.requires(f"lean4-deps/{self._lean_version()}")
        if self.options.jemalloc:
            self.requires("jemalloc/5.3.1")
        self.requires("lz4/1.10.0", force=True)
        self.requires("mpt-crypto/1.0.2", transitive_headers=True)
        self.requires("protobuf/6.33.5", force=True)
        if self.options.rocksdb:
            self.requires("rocksdb/10.5.1")
        self.requires("secp256k1/0.7.1", transitive_headers=True)
        self.requires("sqlite3/3.53.0", force=True)
        self.requires("xxhash/0.8.3", transitive_headers=True)

    exports_sources = (
        "bin/default-loader-path.sh",
        "CMakeLists.txt",
        "cfg/*",
        "cmake/*",
        "external/*",
        "include/*",
        "src/*",
    )

    def layout(self):
        cmake_layout(self)
        # Fix this setting to follow the default introduced in Conan 1.48
        # to align with our build instructions.
        self.folders.generators = "build/generators"

    generators = "CMakeDeps"

    def generate(self):
        # The sources in the Conan cache have no git history, so the version
        # comes from the reference, unless it is not one, like 'develop'.
        if not os.path.exists(os.path.join(self.source_folder, ".git")):
            version = str(self.version)
            env = Environment()
            env.define(
                "FORCE_XRPLD_VERSION",
                version if re.match(r"\d+\.\d+\.\d+", version) else DEV_VERSION,
            )
            env.vars(self).save_script("xrpld_version")

        tc = CMakeToolchain(self)
        tc.variables["tests"] = self.options.tests
        tc.variables["benchmark"] = self.options.benchmark
        tc.variables["assert"] = self.options.assertions
        tc.variables["coverage"] = self.options.coverage
        tc.variables["formal_verification"] = self.options.formal_verification
        if self.options.formal_verification:
            lean4 = self.dependencies["lean4"].cpp_info
            lean4_deps = self.dependencies["lean4-deps"].cpp_info
            self._assert_lake_closure(lean4_deps.get_property("lake_manifest"))
            tc.variables["LEAN4_BINDIR"] = lean4.bindirs[0]
            tc.variables["LEAN4_DEPS_PACKAGES"] = lean4_deps.get_property("packages")
        tc.variables["jemalloc"] = self.options.jemalloc
        tc.variables["rocksdb"] = self.options.rocksdb
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.variables["static"] = self.options.static
        tc.variables["unity"] = self.options.unity
        tc.variables["xrpld"] = self.options.xrpld
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.verbose = True
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.verbose = True
        cmake.install()

    def package_info(self):
        libxrpl = self.cpp_info.components["libxrpl"]
        libxrpl.libs = [
            "xrpl",
            "xrpl.libpb",
        ]
        # TODO: Fix the protobufs to include each other relative to
        # `include/`, not `include/xrpl/proto/`.
        libxrpl.includedirs = ["include", "include/xrpl/proto"]
        libxrpl.requires = [
            "boost::headers",
            "boost::chrono",
            "boost::container",
            "boost::context",
            "boost::date_time",
            "boost::filesystem",
            "boost::json",
            "boost::program_options",
            "boost::process",
            "boost::regex",
            "boost::thread",
            "date::date",
            "ed25519::ed25519",
            "fast_float::fast_float",
            "grpc::grpc++",
            "libarchive::libarchive",
            "lz4::lz4",
            "mpt-crypto::mpt-crypto",
            "nudb::nudb",
            "openssl::crypto",
            "protobuf::libprotobuf",
            "soci::soci",
            "secp256k1::secp256k1",
            "sqlite3::sqlite",
            "xxhash::xxhash",
            "zlib::zlib",
        ]
        if self.options.rocksdb:
            libxrpl.requires.append("rocksdb::librocksdb")
