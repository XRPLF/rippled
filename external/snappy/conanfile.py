from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import apply_conandata_patches, copy, export_conandata_patches, get, rmdir
from conan.tools.scm import Version
import os

required_conan_version = ">=1.54.0"


class SnappyConan(ConanFile):
    name = "snappy"
    description = "A fast compressor/decompressor"
    topics = ("google", "compressor", "decompressor")
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/google/snappy"
    license = "BSD-3-Clause"

    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
    }

    def export_sources(self):
        export_conandata_patches(self)

    def config_options(self):
        if self.settings.os == 'Windows':
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def validate(self):
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, 11)

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["SNAPPY_BUILD_TESTS"] = False
        if Version(self.version) >= "1.1.8":
            tc.variables["SNAPPY_FUZZING_BUILD"] = False
            tc.variables["SNAPPY_REQUIRE_AVX"] = False
            tc.variables["SNAPPY_REQUIRE_AVX2"] = False
            tc.variables["SNAPPY_INSTALL"] = True
        if Version(self.version) >= "1.1.9":
            tc.variables["SNAPPY_BUILD_BENCHMARKS"] = False
        tc.generate()

    def build(self):
        apply_conandata_patches(self)
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "COPYING", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "Snappy")
        self.cpp_info.set_property("cmake_target_name", "Snappy::snappy")
        # TODO: back to global scope in conan v2 once cmake_find_package* generators removed
        self.cpp_info.components["snappylib"].libs = ["snappy"]
        if not self.options.shared:
            if self.settings.os in ["Linux", "FreeBSD"]:
                self.cpp_info.components["snappylib"].system_libs.append("m")

        # TODO: to remove in conan v2 once cmake_find_package* generators removed
        self.cpp_info.names["cmake_find_package"] = "Snappy"
        self.cpp_info.names["cmake_find_package_multi"] = "Snappy"
        self.cpp_info.components["snappylib"].names["cmake_find_package"] = "snappy"
        self.cpp_info.components["snappylib"].names["cmake_find_package_multi"] = "snappy"
        self.cpp_info.components["snappylib"].set_property("cmake_target_name", "Snappy::snappy")
