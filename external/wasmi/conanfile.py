from conan import ConanFile, tools
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import (
    apply_conandata_patches,
    export_conandata_patches,
    # get,
)
from conan.tools.scm import Git

import os
# import json

required_conan_version = ">=1.55.0"

class WasmiConan(ConanFile):
    name = "wasmi"
    license = "Apache License v2.0"
    url = "https://github.com/wasmi-labs/wasmi.git"
    description = "WebAssembly (Wasm) interpreter"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False]}
    default_options = {"shared": False}
    # generators = "CMakeToolchain", "CMakeDeps"
    #requires = [("llvm/20.1.1@")]

    def export_sources(self):
        export_conandata_patches(self)
        pass

    # def build_requirements(self):
    #    self.tool_requires("llvm/20.1.1")


    def config_options(self):
        #if self.settings.os == "Windows":
        #    del self.options.fPIC
        pass


    def layout(self):
        cmake_layout(self, src_folder="src")


    def source(self):
        git = Git(self)
        git.fetch_commit(
            url="https://github.com/wasmi-labs/wasmi.git",
            commit="f628a7a86c9715f2c306f6ef9aea1cc2bdca5fa7",
        )
        #get(self, **self.conan_data["sources"][self.version], strip_root=True)


    def generate(self):
        tc = CMakeToolchain(self)

        tc.variables["CMAKE_CXX_STANDARD"] = 20
        tc.variables["BUILD_SHARED_LIBS"] = 0

        tc.generate()

        # This generates "foo-config.cmake" and "bar-config.cmake" in self.generators_folder
        deps = CMakeDeps(self)
        deps.generate()


    def build(self):
        apply_conandata_patches(self)
        cmake = CMake(self)
        cmake.verbose = True
        cmake.configure(build_script_folder=os.path.join(self.source_folder, "crates", "c_api"))
        cmake.build()
        #self.run(f'echo {self.source_folder}')

        # Explicit way:
        # self.run('cmake %s/hello %s' % (self.source_folder, cmake.command_line))
        # self.run("cmake --build . %s" % cmake.build_config)


    def package(self):
        cmake = CMake(self)
        cmake.verbose = True
        cmake.install()


    def package_info(self):
        self.cpp_info.libs = ["wasmi"]
        self.cpp_info.names["cmake_find_package"] = "wasmi"
        self.cpp_info.names["cmake_find_package_multi"] = "wasmi"

