# build-switch-curl.cmake
#
# Builds a project-local libcurl for Nintendo Switch (devkitA64/libnx)
# via ExternalProject_Add, so the resulting binary is statically linked
# against a curl that has only the protocols NX-ScreenUploader actually
# needs (HTTP/HTTPS).  Unnecessary protocol handlers (FTP, FILE, TELNET,
# …) are compiled out at curl's ./configure time, which is the only place
# they can be removed — --gc-sections in the app's own Makefile cannot
# drop them because curl keeps a single static dispatch table
# (Curl_builtin in lib/url.c) that references every compiled-in handler.
#
# The build uses exactly the same source and patches as devkitPro's
# switch-curl package (curl 7.69.1 + libnx TLS-backend patch), so
# HTTPS works out of the box via the console's ssl: sysmodule.  The
# resulting libcurl.a is installed to a local prefix *inside* the build
# tree — no system modification, no sudo, no pollution of the devkitPro
# environment.  Also applies the one-line upstream fix for curl/curl#5126
# (broken AC_REQUIRE in m4/curl-functions.m4) so a modern autoconf
# (≥ 2.70) works fine.
#
# To use from CMakeLists.txt:
#
#   option(BUILD_CURL_LOCAL "…" ON)
#   if(BUILD_CURL_LOCAL)
#     include(build-switch-curl)
#   else()
#     find_package(CURL REQUIRED)
#   endif()
#   target_link_libraries(myapp CURL::libcurl …)
#
# Prerequisites (same as any devkitPro homebrew project):
#   - DEVKITPRO environment variable
#   - devkitA64 + libnx + switch-zlib installed via (dkp-)pacman
#   - Host tools: autoconf, automake, libtool, pkg-config, patch, tar, make

# ---------------------------------------------------------------------------
# Configuration — matches devkitPro's switch-curl PKGBUILD
# ---------------------------------------------------------------------------

set(_curl_version "7.69.1")
set(_curl_url "https://curl.se/download/curl-${_curl_version}.tar.xz")
set(_curl_sha256 "03c7d5e6697f7b7e40ada1b2256e565a555657398e6c1fcfa4cb251ccd819d4f")

set(_curl_patch_url
  "https://raw.githubusercontent.com/devkitPro/pacman-packages/master/switch/curl/switch-curl.patch")
set(_curl_patch_sha256
  "723c7d884fc7c39ae1a3115ba245bb8c1415da47bbd60ab8f943ca98f92ebc9a")

# Install prefix *inside* the CMake build tree — no system paths touched.
set(_curl_prefix "${CMAKE_BINARY_DIR}/curl-install")

# Protocols that most Switch homebrew never uses.  Grep your own source
# for curl_easy_setopt calls before removing anything from this list.
set(_curl_disable_protos
  ftp file ldap ldaps rtsp dict telnet tftp pop3
  imap smtp smb gopher mqtt
)

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------

if(NOT DEFINED DEVKITPRO)
  message(FATAL_ERROR
    "BUILD_CURL_LOCAL requires DEVKITPRO.  "
    "Set the environment variable or use the devkitA64 toolchain file.")
endif()

# ---------------------------------------------------------------------------
# Download the devkitPro patch at *configure* time so it's available when
# ExternalProject_Add's PATCH_COMMAND runs at build time.
# ---------------------------------------------------------------------------

set(_curl_patch_file "${CMAKE_BINARY_DIR}/curl-patch/switch-curl.patch")
file(DOWNLOAD "${_curl_patch_url}" "${_curl_patch_file}"
  EXPECTED_HASH SHA256=${_curl_patch_sha256}
  SHOW_PROGRESS
)

# ---------------------------------------------------------------------------
# Build tools & flags (mirrors build_switch_curl.py + devkita64-libnx.cmake)
# ---------------------------------------------------------------------------

set(_curl_tool_prefix "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-")
set(_curl_arch
  "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec")
set(_curl_cflags
  "${_curl_arch} -O2 -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables")
set(_curl_cppflags
  "-D__SWITCH__ -I${PORTLIBS}/include -isystem ${LIBNX}/include")
set(_curl_ldflags
  "${_curl_arch} -L${PORTLIBS}/lib -L${LIBNX}/lib -specs=${DEVKITPRO}/libnx/switch.specs")
set(_curl_libs "-lnx")

# Build the --disable-* argument list.
set(_curl_disable_args "")
foreach(p ${_curl_disable_protos})
  list(APPEND _curl_disable_args "--disable-${p}")
endforeach()

# Figure out how many parallel jobs to use for the curl build.
set(_curl_jobs "${CMAKE_BUILD_PARALLEL_LEVEL}")
if(NOT _curl_jobs)
  execute_process(COMMAND nproc
    OUTPUT_VARIABLE _curl_jobs
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT _curl_jobs)
    set(_curl_jobs 2)
  endif()
endif()

# ---------------------------------------------------------------------------
# Configure wrapper script
#
# ExternalProject_Add does not run commands through a shell, so we cannot
# simply prepend "CC=… ./configure".  Instead we generate a short shell
# script that exports the cross-compile environment and then exec's
# whatever it receives as arguments.
# ---------------------------------------------------------------------------

set(_curl_wrapper "${CMAKE_BINARY_DIR}/curl/curl-configure.sh")
file(WRITE "${_curl_wrapper}" "\
#!/bin/bash
# Generated by build-switch-curl.cmake — do not edit.
set -e
export CC=\"${_curl_tool_prefix}gcc\"
export CXX=\"${_curl_tool_prefix}g++\"
export AR=\"${_curl_tool_prefix}gcc-ar\"
export RANLIB=\"${_curl_tool_prefix}gcc-ranlib\"
export CFLAGS=\"${_curl_cflags}\"
export CPPFLAGS=\"${_curl_cppflags}\"
export LDFLAGS=\"${_curl_ldflags}\"
export LIBS=\"${_curl_libs}\"
export cross_compiling=yes
exec \"\$@\"
")
execute_process(COMMAND chmod +x "${_curl_wrapper}")

# ---------------------------------------------------------------------------
# ExternalProject_Add — download, patch, buildconf, configure, make, install
# ---------------------------------------------------------------------------

include(ExternalProject)

# Create the install prefix early so CMake doesn't warn about
# non-existent INTERFACE_INCLUDE_DIRECTORIES on the imported target.
file(MAKE_DIRECTORY "${_curl_prefix}/include")

ExternalProject_Add(curl_local
  URL               "${_curl_url}"
  URL_HASH          SHA256=${_curl_sha256}
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  PREFIX            "${CMAKE_BINARY_DIR}/curl"
  INSTALL_DIR       "${_curl_prefix}"
  # curl's autoconf build is in-source.
  BUILD_IN_SOURCE   TRUE

  # ---- patch -----------------------------------------------------------
  # 1. Apply devkitPro's switch-curl.patch (adds libnx TLS backend).
  # 2. Fix curl/curl#5126: remove the broken AC_REQUIRE([AC_RUN_IFELSE])
  #    line from m4/curl-functions.m4 so that autoconf ≥ 2.70 works during
  #    cross compilation.
  PATCH_COMMAND
    COMMAND patch -Np1 -i "${_curl_patch_file}"
    COMMAND sed -i "/AC_REQUIRE(\\[AC_RUN_IFELSE\\])/d"
            m4/curl-functions.m4

  # ---- configure -------------------------------------------------------
  # buildconf generates the configure script from the autotools sources.
  CONFIGURE_COMMAND
    COMMAND "${_curl_wrapper}" ./buildconf
    COMMAND "${_curl_wrapper}" ./configure
      --prefix=${_curl_prefix}
      --host=aarch64-none-elf
      --disable-shared
      --enable-static
      --disable-ipv6
      --disable-unix-sockets
      --disable-manual
      --disable-ntlm-wb
      --disable-threaded-resolver
      --without-ssl
      --without-polarssl
      --without-cyassl
      --without-wolfssl
      --without-mbedtls
      --without-bearssl
      --without-rustls
      --with-libnx
      --with-default-ssl-backend=libnx
      ${_curl_disable_args}

  # ---- build -----------------------------------------------------------
  # Only the library is needed, not tests, examples, or docs.
  BUILD_COMMAND
    "${_curl_wrapper}" make -C lib -j${_curl_jobs}

  # ---- install ---------------------------------------------------------
  INSTALL_COMMAND
    COMMAND "${_curl_wrapper}" make -C lib install
    COMMAND "${_curl_wrapper}" make -C include install
    COMMAND "${_curl_wrapper}" make install-pkgconfigDATA

  # Tell the Ninja / Make generator that this file is produced by the
  # external project (avoids "no rule to make target" errors).
  BUILD_BYPRODUCTS "${_curl_prefix}/lib/libcurl.a"

  # Keep the build logs visible for debugging CI failures.
  LOG_DOWNLOAD   ON
  LOG_CONFIGURE  ON
  LOG_BUILD      ON
  LOG_INSTALL    ON
)

# ---------------------------------------------------------------------------
# Expose the locally built curl as an imported target
# ---------------------------------------------------------------------------

set(CURL_FOUND        ON  CACHE INTERNAL "")
set(CURL_LIBRARIES    "${_curl_prefix}/lib/libcurl.a"  CACHE FILEPATH "")
set(CURL_INCLUDE_DIRS "${_curl_prefix}/include"        CACHE PATH     "")

# Also provide the modern CMake target (same name as FindCURL creates).
if(NOT TARGET CURL::libcurl)
  add_library(CURL::libcurl STATIC IMPORTED GLOBAL)
  set_target_properties(CURL::libcurl PROPERTIES
    IMPORTED_LOCATION              "${_curl_prefix}/lib/libcurl.a"
    INTERFACE_INCLUDE_DIRECTORIES  "${_curl_prefix}/include"
  )
  add_dependencies(CURL::libcurl curl_local)
endif()

# ---------------------------------------------------------------------------
# Clean up internal variables so they don't leak into the parent scope.
# ---------------------------------------------------------------------------
unset(_curl_version)
unset(_curl_url)
unset(_curl_sha256)
unset(_curl_patch_url)
unset(_curl_patch_sha256)
unset(_curl_prefix)
unset(_curl_disable_protos)
unset(_curl_patch_file)
unset(_curl_tool_prefix)
unset(_curl_arch)
unset(_curl_cflags)
unset(_curl_cppflags)
unset(_curl_ldflags)
unset(_curl_libs)
unset(_curl_disable_args)
unset(_curl_wrapper)
unset(_curl_jobs)
