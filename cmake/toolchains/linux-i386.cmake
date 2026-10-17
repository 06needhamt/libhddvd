# 32-bit x86 build on a 64-bit Debian/Ubuntu host with multiarch packages
# (gcc-multilib, g++-multilib, libxml2-dev:i386). Used by the CI 32-bit job;
# the install-consumer test passes it on to the consumer projects.
set(CMAKE_C_FLAGS_INIT "-m32")
set(CMAKE_CXX_FLAGS_INIT "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-m32")
set(CMAKE_LIBRARY_ARCHITECTURE i386-linux-gnu)
set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS FALSE)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")
