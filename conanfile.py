import re

from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout

from conan import ConanFile
from conan import __version__ as conan_version


class Xrpl(ConanFile):
    name = "xrpl"

    license = "ISC"
    author = "John Freeman <jfreeman@ripple.com>"
    url = "https://github.com/xrplf/rippled"
    description = "The XRP Ledger"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "assertions": [True, False],
        "coverage": [True, False],
        "fPIC": [True, False],
        "jemalloc": [True, False],
        "rocksdb": [True, False],
        "shared": [True, False],
        "static": [True, False],
        "tests": [True, False],
        "unity": [True, False],
        "xrpld": [True, False],
    }

    requires = [
        "grpc/1.50.1",
        "libarchive/3.8.1",
        "nudb/2.0.9",
        "openssl/3.5.4",
        "soci/4.0.3",
        "zlib/1.3.1",
    ]

    test_requires = [
        "doctest/2.4.12",
    ]

    tool_requires = [
        "protobuf/3.21.12",
    ]

    default_options = {
        "assertions": False,
        "coverage": False,
        "fPIC": True,
        "jemalloc": False,
        "rocksdb": True,
        "shared": False,
        "static": True,
        "tests": False,
        "unity": False,
        "xrpld": False,
        "date/*:header_only": True,
        "grpc/*:shared": False,
        "grpc/*:secure": True,
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
        "openssl/*:shared": False,
        "protobuf/*:shared": False,
        "protobuf/*:with_zlib": True,
        "rocksdb/*:enable_sse": False,
        "rocksdb/*:lite": False,
        "rocksdb/*:shared": False,
        "rocksdb/*:use_rtti": True,
        "rocksdb/*:with_jemalloc": False,
        "rocksdb/*:with_lz4": True,
        "rocksdb/*:with_snappy": True,
        "snappy/*:shared": False,
        "soci/*:shared": False,
        "soci/*:with_sqlite3": True,
        "soci/*:with_boost": True,
        "xxhash/*:shared": False,
    }

    def set_version(self):
        if self.version is None:
            path = f"{self.recipe_folder}/src/libxrpl/protocol/BuildInfo.cpp"
            regex = r"versionString\s?=\s?\"(.*)\""
            with open(path, encoding="utf-8") as file:
                matches = (re.search(regex, line) for line in file)
                match = next(m for m in matches if m)
                self.version = match.group(1)

    def configure(self):
        if self.settings.compiler == "apple-clang":
            self.options["boost"].visibility = "global"
        if self.settings.compiler in ["clang", "gcc"]:
            self.options["boost"].without_cobalt = True

    def requirements(self):
        # Conan 2 requires transitive headers to be specified
        transitive_headers_opt = (
            {"transitive_headers": True} if conan_version.split(".")[0] == "2" else {}
        )
        self.requires("boost/1.88.0", force=True, **transitive_headers_opt)
        self.requires("date/3.0.4", **transitive_headers_opt)
        self.requires("lz4/1.10.0", force=True)
        self.requires("protobuf/3.21.12", force=True)
        self.requires("sqlite3/3.49.1", force=True)
        if self.options.jemalloc:
            self.requires("jemalloc/5.3.0")
        if self.options.rocksdb:
            self.requires("rocksdb/10.5.1")
        self.requires("xxhash/0.8.3", **transitive_headers_opt)

    exports_sources = (
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
        tc = CMakeToolchain(self)
        tc.variables["tests"] = self.options.tests
        tc.variables["assert"] = self.options.assertions
        tc.variables["coverage"] = self.options.coverage
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
            "ed25519",
            "secp256k1",
        ]
        # TODO: Fix the protobufs to include each other relative to
        # `include/`, not `include/ripple/proto/`.
        libxrpl.includedirs = ["include", "include/ripple/proto"]
        libxrpl.requires = [
            "boost::headers",
            "boost::chrono",
            "boost::container",
            "boost::coroutine",
            "boost::date_time",
            "boost::filesystem",
            "boost::json",
            "boost::program_options",
            "boost::process",
            "boost::regex",
            "boost::system",
            "boost::thread",
            "date::date",
            "grpc::grpc++",
            "libarchive::libarchive",
            "lz4::lz4",
            "nudb::nudb",
            "openssl::crypto",
            "protobuf::libprotobuf",
            "soci::soci",
            "sqlite3::sqlite",
            "xxhash::xxhash",
            "zlib::zlib",
        ]
        if self.options.rocksdb:
            libxrpl.requires.append("rocksdb::librocksdb")
