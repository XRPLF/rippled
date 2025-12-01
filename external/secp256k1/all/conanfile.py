from conan import ConanFile, tools
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import get

required_conan_version = ">=2.0.0"


class SecpConan(ConanFile):
    name = "secp256k1"
    description = "High-performance high-assurance C library for digital signatures and other cryptographic primitives on the secp256k1 elliptic curve."
    url = "https://github.com/bitcoin-core/secp256k1.git"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        # On Windows, SECP256K1_STATIC must be defined when consuming secp256k1 as a
        # static library, which gets set if BUILD_SHARED_LIBS is false.
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        # Disable all tests and benchmarks.
        tc.variables["SECP256K1_INSTALL"] = True
        tc.variables["SECP256K1_BUILD_BENCHMARK"] = False
        tc.variables["SECP256K1_BUILD_TESTS"] = False
        tc.variables["SECP256K1_BUILD_EXHAUSTIVE_TESTS"] = False
        tc.variables["SECP256K1_BUILD_CTIME_TESTS"] = False
        tc.variables["SECP256K1_BUILD_EXAMPLES"] = False
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["secp256k1"]
        self.cpp_info.set_property("cmake_file_name", "secp256k1")
        self.cpp_info.set_property("cmake_target_name", "secp256k1::secp256k1")
