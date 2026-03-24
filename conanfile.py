from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps


class cfl_libraryRecipe(ConanFile):
    name = "sdbrdd"
    version = "1.5.0"
    package_type = "library"

    # Optional metadata
    license = "ASF 2.0"
    author = "Fabio Uggeri fabiouggeri@gmail.com"
    url = "https://github.com/fabiouggeri/sdbrdd"
    description = "Database driver for Harbour."
    topics = ("Harbour", "RDD", "Database", "Oracle", "library")
    #generators = "CMakeToolchain", "CMakeDeps"

    #requires = "yapp-c-runtime-lib/1.0.4", "cfl-library/1.10.0"

    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "CMakeLists.txt", "cmake/HarbourCompiler.cmake", "sdb-rdd-headers/**", "sdb-rdd-lib/**", "sdb-ora-headers/**", "sdb-ora-lib/**"

    def requirements(self):
        self.requires("cfl-library/1.10.0")
        self.requires("yapp-c-runtime/1.0.4")

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        # self.folders.build = "build"
        # self.folders.generators = "build"
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["sdb-rdd-lib", "sdb-ora-lib"]

