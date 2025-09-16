from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get
from conan.tools.scm import Version
import os

required_conan_version = ">=1.54.0"


class Blake3Conan(ConanFile):
    name = "blake3"
    version = "1.5.0"
    description = "BLAKE3 cryptographic hash function"
    topics = ("blake3", "hash", "cryptography")
    url = "https://github.com/BLAKE3-team/BLAKE3"
    homepage = "https://github.com/BLAKE3-team/BLAKE3"
    license = "CC0-1.0 OR Apache-2.0"
    
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "simd": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "simd": True,
    }

    def config_options(self):
        if self.settings.os == 'Windows':
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        # BLAKE3 is C code
        self.settings.rm_safe("compiler.cppstd")
        self.settings.rm_safe("compiler.libcxx")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        # BLAKE3's CMake options
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        if not self.options.simd:
            tc.variables["BLAKE3_NO_SSE2"] = True
            tc.variables["BLAKE3_NO_SSE41"] = True
            tc.variables["BLAKE3_NO_AVX2"] = True
            tc.variables["BLAKE3_NO_AVX512"] = True
            tc.variables["BLAKE3_NO_NEON"] = True
        tc.generate()

    def build(self):
        cmake = CMake(self)
        # BLAKE3's C implementation has its CMakeLists.txt in the c/ subdirectory
        cmake.configure(build_script_folder=os.path.join(self.source_folder, "c"))
        cmake.build()

    def package(self):
        # Copy license files
        copy(self, "LICENSE*", src=self.source_folder, 
             dst=os.path.join(self.package_folder, "licenses"))
        # Copy header
        copy(self, "blake3.h", 
             src=os.path.join(self.source_folder, "c"),
             dst=os.path.join(self.package_folder, "include"))
        # Copy library
        copy(self, "*.a", src=self.build_folder, 
             dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.lib", src=self.build_folder, 
             dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dylib", src=self.build_folder, 
             dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.so*", src=self.build_folder, 
             dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dll", src=self.build_folder, 
             dst=os.path.join(self.package_folder, "bin"), keep_path=False)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "BLAKE3")
        self.cpp_info.set_property("cmake_target_name", "BLAKE3::blake3")
        
        # IMPORTANT: Explicitly set include directories to fix Conan CMakeDeps generation
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libs = ["blake3"]
        
        # System libraries
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs.append("m")
            self.cpp_info.system_libs.append("pthread")
        
        # TODO: to remove in conan v2 once cmake_find_package* generators removed
        self.cpp_info.names["cmake_find_package"] = "BLAKE3"
        self.cpp_info.names["cmake_find_package_multi"] = "BLAKE3"