from conan import ConanFile, tools
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import (
    apply_conandata_patches,
    export_conandata_patches,
    # get,
)
from conan.tools.scm import Git

# import os

required_conan_version = ">=1.55.0"


class WamrConan(ConanFile):
    name = "wamr"
    version = "2.3.1"
    license = "Apache License v2.0"
    url = "https://github.com/bytecodealliance/wasm-micro-runtime.git"
    description = "Webassembly micro runtime"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    # requires = [("llvm/20.1.1@")]

    def export_sources(self):
        export_conandata_patches(self)
        pass

    # def build_requirements(self):
    #    self.tool_requires("llvm/20.1.1")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        git = Git(self)
        git.fetch_commit(
            url="https://github.com/bytecodealliance/wasm-micro-runtime.git",
            commit="2a303861cc916dc182b7fecaa0aacc1b797e7ac6",
        )
        # get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)

        tc.variables["WAMR_BUILD_INTERP"] = 1
        tc.variables["WAMR_BUILD_FAST_INTERP"] = 1
        tc.variables["WAMR_BUILD_INSTRUCTION_METERING"] = 1
        tc.variables["WAMR_BUILD_AOT"] = 0
        tc.variables["WAMR_BUILD_JIT"] = 0
        tc.variables["WAMR_BUILD_FAST_JIT"] = 0
        tc.variables["WAMR_DISABLE_HW_BOUND_CHECK"] = 1
        tc.variables["WAMR_DISABLE_STACK_HW_BOUND_CHECK"] = 1
        tc.variables["WAMR_BH_LOG"] = "wamr_log_to_rippled"
        # tc.variables["WAMR_BUILD_FAST_JIT"] = 0 if self.settings.os == "Windows" else 1
        # ll_dep = self.dependencies["llvm"]
        # self.output.info(f"-----------package_folder: {type(ll_dep.__dict__)}")
        # tc.variables["LLVM_DIR"] = os.path.join(ll_dep.package_folder, "lib", "cmake", "llvm")
        tc.generate()

        # This generates "foo-config.cmake" and "bar-config.cmake" in self.generators_folder
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        apply_conandata_patches(self)
        cmake = CMake(self)
        cmake.verbose = True
        cmake.configure()
        cmake.build()
        # self.run(f'echo {self.source_folder}')
        # Explicit way:
        # self.run('cmake %s/hello %s' % (self.source_folder, cmake.command_line))
        # self.run("cmake --build . %s" % cmake.build_config)

    def package(self):
        cmake = CMake(self)
        cmake.verbose = True
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["iwasm"]
        self.cpp_info.names["cmake_find_package"] = "wamr"
        self.cpp_info.names["cmake_find_package_multi"] = "wamr"
