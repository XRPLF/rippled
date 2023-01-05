from conans import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
import re

class Xrpl(ConanFile):
    name = 'xrpl'

    license = 'ISC'
    author = 'John Freeman <jfreeman@ripple.com>'
    url = 'https://github.com/xrplf/rippled'
    description = 'The XRP Ledger'
    settings = 'os', 'compiler', 'build_type', 'arch'
    options = {
        'assertions': [True, False],
        'coverage': [True, False],
        'fPIC': [True, False],
        'jemalloc': [True, False],
        'reporting': [True, False],
        'rocksdb': [True, False],
        'shared': [True, False],
        'static': [True, False],
        'tests': [True, False],
        'unity': [True, False],
    }

    requires = [
        'boost/1.77.0',
        'date/3.0.1',
        'libarchive/3.6.0',
        'lz4/1.9.3',
        'grpc/1.44.0',
        'nudb/2.0.8',
        'openssl/1.1.1m',
        'protobuf/3.21.4',
        'snappy/1.1.9',
        'soci/4.0.3',
        'sqlite3/3.38.0',
        'zlib/1.2.12',
    ]

    default_options = {
        'assertions': False,
        'coverage': False,
        'fPIC': True,
        'jemalloc': False,
        'reporting': False,
        'rocksdb': True,
        'shared': False,
        'static': True,
        'tests': True,
        'unity': False,

        'cassandra-cpp-driver:shared': False,
        'date:header_only': True,
        'grpc:shared': False,
        'grpc:secure': True,
        'libarchive:shared': False,
        'libarchive:with_acl': False,
        'libarchive:with_bzip2': False,
        'libarchive:with_cng': False,
        'libarchive:with_expat': False,
        'libarchive:with_iconv': False,
        'libarchive:with_libxml2': False,
        'libarchive:with_lz4': True,
        'libarchive:with_lzma': False,
        'libarchive:with_lzo': False,
        'libarchive:with_nettle': False,
        'libarchive:with_openssl': False,
        'libarchive:with_pcreposix': False,
        'libarchive:with_xattr': False,
        'libarchive:with_zlib': False,
        'libpq:shared': False,
        'lz4:shared': False,
        'openssl:shared': False,
        'protobuf:shared': False,
        'protobuf:with_zlib': True,
        'rocksdb:enable_sse': False,
        'rocksdb:lite': False,
        'rocksdb:shared': False,
        'rocksdb:use_rtti': True,
        'rocksdb:with_jemalloc': False,
        'rocksdb:with_lz4': True,
        'rocksdb:with_snappy': True,
        'snappy:shared': False,
        'soci:shared': False,
        'soci:with_sqlite3': True,
        'soci:with_boost': True,
    }

    def set_version(self):
        path = f'{self.recipe_folder}/src/ripple/protocol/impl/BuildInfo.cpp'
        regex = r'versionString\s?=\s?\"(.*)\"'
        with open(path, 'r') as file:
            matches = (re.search(regex, line) for line in file)
            match = next(m for m in matches if m)
            self.version = match.group(1)

    def configure(self):
        if self.settings.compiler == 'apple-clang':
            self.options['boost'].visibility = 'global'

    def requirements(self):
        if self.options.jemalloc:
            self.requires('jemalloc/5.2.1')
        if self.options.reporting:
            self.requires('cassandra-cpp-driver/2.15.3')
            self.requires('libpq/13.6')
        if self.options.rocksdb:
            self.requires('rocksdb/6.27.3')

    exports_sources = 'CMakeLists.txt', 'Builds/CMake/*', 'src/*', 'cfg/*'

    def layout(self):
        cmake_layout(self)
        # Fix this setting to follow the default introduced in Conan 1.48
        # to align with our build instructions.
        self.folders.generators = 'build/generators'

    generators = 'CMakeDeps'
    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables['tests'] = self.options.tests
        tc.variables['assert'] = self.options.assertions
        tc.variables['coverage'] = self.options.coverage
        tc.variables['jemalloc'] = self.options.jemalloc
        tc.variables['reporting'] = self.options.reporting
        tc.variables['rocksdb'] = self.options.rocksdb
        tc.variables['BUILD_SHARED_LIBS'] = self.options.shared
        tc.variables['static'] = self.options.static
        tc.variables['unity'] = self.options.unity
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
        self.cpp_info.libs = [
            'libxrpl_core.a',
            'libed25519-donna.a',
            'libsecp256k1.a',
        ]
