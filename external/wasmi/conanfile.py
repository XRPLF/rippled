from conan import ConanFile, tools
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import apply_conandata_patches, export_conandata_patches, get
import os

required_conan_version = ">=2.0.0"

class WasmiConan(ConanFile):
    name = "wasmi"
    license = "Apache License v2.0"
    url = "https://github.com/wasmi-labs/wasmi.git"
    description = "WebAssembly (Wasm) interpreter"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [False]}
    default_options = {"shared": False}

    def export_sources(self):
        export_conandata_patches(self)

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)
        apply_conandata_patches(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure(build_script_folder=os.path.join(self.source_folder, "crates", "c_api"))
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["wasmi"]
        self.cpp_info.names["cmake_find_package"] = "wasmi"
        self.cpp_info.names["cmake_find_package_multi"] = "wasmi"
