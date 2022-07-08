import os
import shutil
from conans import ConanFile, CMake
from conan.tools import microsoft as ms

class RocksDB(ConanFile):
    name = 'rocksdb'
    version = '6.27.3'

    license = ('GPL-2.0-only', 'Apache-2.0')
    url = 'https://github.com/conan-io/conan-center-index'
    description = 'A library that provides an embeddable, persistent key-value store for fast storage'
    topics = ('rocksdb', 'database', 'leveldb', 'facebook', 'key-value')

    settings = 'os', 'compiler', 'build_type', 'arch'
    options = {
        'enable_sse': [False, 'sse42', 'avx2'],
        'fPIC': [True, False],
        'lite': [True, False],
        'shared': [True, False],
        'use_rtti': [True, False],
        'with_gflags': [True, False],
        'with_jemalloc': [True, False],
        'with_lz4': [True, False],
        'with_snappy': [True, False],
        'with_tbb': [True, False],
        'with_zlib': [True, False],
        'with_zstd': [True, False],
    }
    default_options = {
        'enable_sse': False,
        'fPIC': True,
        'lite': False,
        'shared': False,
        'use_rtti': False,
        'with_gflags': False,
        'with_jemalloc': False,
        'with_lz4': False,
        'with_snappy': False,
        'with_tbb': False,
        'with_zlib': False,
        'with_zstd': False,
    }

    def requirements(self):
        if self.options.with_gflags:
            self.requires('gflags/2.2.2')
        if self.options.with_jemalloc:
            self.requires('jemalloc/5.2.1')
        if self.options.with_lz4:
            self.requires('lz4/1.9.3')
        if self.options.with_snappy:
            self.requires('snappy/1.1.9')
        if self.options.with_tbb:
            self.requires('onetbb/2020.3')
        if self.options.with_zlib:
            self.requires('zlib/1.2.11')
        if self.options.with_zstd:
            self.requires('zstd/1.5.2')

    def config_options(self):
        if self.settings.os == 'Windows':
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            del self.options.fPIC

    generators = 'cmake', 'cmake_find_package'

    scm = {
        'type': 'git',
        'url': 'https://github.com/facebook/rocksdb.git',
        'revision': 'v6.27.3',
    }

    exports_sources = 'thirdparty.inc'
    # For out-of-source build.
    no_copy_source = True

    _cmake = None

    def _configure_cmake(self):
        if self._cmake:
            return

        self._cmake = CMake(self)

        self._cmake.definitions['CMAKE_POSITION_INDEPENDENT_CODE'] = True

        self._cmake.definitions['DISABLE_STALL_NOTIF'] = False
        self._cmake.definitions['FAIL_ON_WARNINGS'] = False
        self._cmake.definitions['OPTDBG'] = True
        self._cmake.definitions['WITH_TESTS'] = False
        self._cmake.definitions['WITH_TOOLS'] = False

        self._cmake.definitions['WITH_GFLAGS'] = self.options.with_gflags
        self._cmake.definitions['WITH_JEMALLOC'] = self.options.with_jemalloc
        self._cmake.definitions['WITH_LZ4'] = self.options.with_lz4
        self._cmake.definitions['WITH_SNAPPY'] = self.options.with_snappy
        self._cmake.definitions['WITH_TBB'] = self.options.with_tbb
        self._cmake.definitions['WITH_ZLIB'] = self.options.with_zlib
        self._cmake.definitions['WITH_ZSTD'] = self.options.with_zstd

        self._cmake.definitions['USE_RTTI'] = self.options.use_rtti
        self._cmake.definitions['ROCKSDB_LITE'] = self.options.lite
        self._cmake.definitions['ROCKSDB_INSTALL_ON_WINDOWS'] = (
            self.settings.os == 'Windows'
        )

        if not self.options.enable_sse:
            self._cmake.definitions['PORTABLE'] = True
            self._cmake.definitions['FORCE_SSE42'] = False
        elif self.options.enable_sse == 'sse42':
            self._cmake.definitions['PORTABLE'] = True
            self._cmake.definitions['FORCE_SSE42'] = True
        elif self.options.enable_sse == 'avx2':
            self._cmake.definitions['PORTABLE'] = False
            self._cmake.definitions['FORCE_SSE42'] = False

        self._cmake.definitions['WITH_ASAN'] = False
        self._cmake.definitions['WITH_BZ2'] = False
        self._cmake.definitions['WITH_JNI'] = False
        self._cmake.definitions['WITH_LIBRADOS'] = False
        if ms.is_msvc(self):
            self._cmake.definitions['WITH_MD_LIBRARY'] = (
                ms.msvc_runtime_flag(self).startswith('MD')
            )
            self._cmake.definitions['WITH_RUNTIME_DEBUG'] = (
                ms.msvc_runtime_flag(self).endswith('d')
            )
        self._cmake.definitions['WITH_NUMA'] = False
        self._cmake.definitions['WITH_TSAN'] = False
        self._cmake.definitions['WITH_UBSAN'] = False
        self._cmake.definitions['WITH_WINDOWS_UTF8_FILENAMES'] = False
        self._cmake.definitions['WITH_XPRESS'] = False
        self._cmake.definitions['WITH_FALLOCATE'] = True


    def build(self):
        if ms.is_msvc(self):
            file = os.path.join(
                self.recipe_folder, '..', 'export_source', 'thirdparty.inc'
            )
            shutil.copy(file, self.build_folder)
        self._configure_cmake()
        self._cmake.configure()
        self._cmake.build()

    def package(self):
        self._configure_cmake()
        self._cmake.install()

    def package_info(self):
        self.cpp_info.filenames['cmake_find_package'] = 'RocksDB'
        self.cpp_info.filenames['cmake_find_package_multi'] = 'RocksDB'
        self.cpp_info.set_property('cmake_file_name', 'RocksDB')

        self.cpp_info.names['cmake_find_package'] = 'RocksDB'
        self.cpp_info.names['cmake_find_package_multi'] = 'RocksDB'

        self.cpp_info.components['librocksdb'].names['cmake_find_package'] = 'rocksdb'
        self.cpp_info.components['librocksdb'].names['cmake_find_package_multi'] = 'rocksdb'
        self.cpp_info.components['librocksdb'].set_property(
            'cmake_target_name', 'RocksDB::rocksdb'
        )

        self.cpp_info.components['librocksdb'].libs = ['rocksdb']

        if self.settings.os == "Windows":
            self.cpp_info.components["librocksdb"].system_libs = ["shlwapi", "rpcrt4"]
            if self.options.shared:
                self.cpp_info.components["librocksdb"].defines = ["ROCKSDB_DLL"]
        elif self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.components["librocksdb"].system_libs = ["pthread", "m"]

        if self.options.lite:
            self.cpp_info.components["librocksdb"].defines.append("ROCKSDB_LITE")

        if self.options.with_gflags:
            self.cpp_info.components["librocksdb"].requires.append("gflags::gflags")
        if self.options.with_jemalloc:
            self.cpp_info.components["librocksdb"].requires.append("jemalloc::jemalloc")
        if self.options.with_lz4:
            self.cpp_info.components["librocksdb"].requires.append("lz4::lz4")
        if self.options.with_snappy:
            self.cpp_info.components["librocksdb"].requires.append("snappy::snappy")
        if self.options.with_tbb:
            self.cpp_info.components["librocksdb"].requires.append("onetbb::onetbb")
        if self.options.with_zlib:
            self.cpp_info.components["librocksdb"].requires.append("zlib::zlib")
        if self.options.with_zstd:
            self.cpp_info.components["librocksdb"].requires.append("zstd::zstd")
