
set(fmt_sources)
if(NOT HAVE_FMT)
    set(fmt_sources
        embedded/fmt/format.cc
        embedded/fmt/os.cc
        )
endif()

set (sources 
        cado_poly.c timing.cpp getprime.c mod_ul.c relation.cpp
        relation-tools.c
        modredc_ul.c modredc_15ul.c modredc_2ul2.c modul_poly.c gmp_aux.c
        mod64.cpp modredc64.cpp modredc126.cpp mod_mpz_new.cpp
        double_poly.cpp rootfinder.cpp params.cpp params-extra.cpp gcd.c
        gzip.cpp misc.c misc2.cpp
        mpz_mat.cpp mpz_poly.cpp crc.c purgedfile.c bit_vector.c
        barrier.c usp.c fix-endianness.c memusage.cpp
        cachesize_cpuid.c cachesize_guess.c roots_mod.c mod_mpz.c
        renumber.cpp
        renumber_proxy.cpp
        ringbuf.c sm_utils.cpp galois_action.cpp memalloc.c
	mpz_vector.c memory.c stats.c lll.c
        verbose.c 
        polyroots.c 
        tdict.cpp
        version_info.c
	modified_files.c threadpool.cpp mpz_poly_bivariate.cpp
        gmpxx.cpp
        rho.c gpf.cpp
        sha1.c
        fd_streambuf.cpp
        cado_pipe_streambuf.cpp
        numbertheory.cpp
        badideals.cpp
        json.cpp
        indexed_relation.cpp
        cado-sighandlers.cpp
        mmap_allocator.cpp
        ${fmt_sources}
)

if (NOT MINGW)
    set (sources ${sources} cado_popen.c)
endif()
if(HAVE_HWLOC)
    set (sources ${sources} hwloc-aux.c)
endif()

set (sources_io
        filter_io.cpp
)

############################################################
# Get a list of modified files in the working directory,
# with their SHA1 sums, and write as a string constant to
# modified_files.c

# How we do this mechanism of generating the modified files is very quirky,
# and has varied with cmake versions. See http://public.kitware.com/Bug/view.php?id=11332
#
# The following seems to work also with pre-2.8.4 versions

add_custom_target(list_modified_files
    COMMAND "${CADO_NFS_SOURCE_DIR}/scripts/list_modified_files.sh" "${CMAKE_CURRENT_BINARY_DIR}/modified_files.c"
    WORKING_DIRECTORY "${CADO_NFS_SOURCE_DIR}"
    COMMENT "Generating list of modified files in working tree"
)
configure_file(version_info.c.in version_info.c ESCAPE_QUOTES @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/version_info.c" PROPERTIES GENERATED TRUE)

# the utils library is used by some shared objects (prominently the
# matmul shared libs in bwc), and therefore must be shared.
add_library (utils ${CADO_SHARED} ${sources})
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/modified_files.c" PROPERTIES GENERATED TRUE)
add_dependencies(utils list_modified_files)
set_target_properties(utils PROPERTIES COMPILE_FLAGS "${OpenMP_C_FLAGS}")
target_link_libraries(utils pthread ${gmp_libname} m "${OpenMP_C_FLAGS}")
if(HAVE_HWLOC)
target_link_libraries(utils hwloc)
endif()
if(HAVE_FMT)
target_link_libraries(utils fmt)
endif()

add_library (utils_with_io ${CADO_SHARED} ${sources_io})
target_link_libraries(utils_with_io utils)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    message(STATUS "Disabling antebuffer on ${CMAKE_SYSTEM_NAME}, see #21410")
    # https://gitlab.inria.fr/cado-nfs/cado-nfs/-/issues/21410
else()
    add_executable(antebuffer antebuffer.c timing.cpp memusage.cpp
        ${fmt_sources})
    target_link_libraries(antebuffer pthread)
    if(HAVE_FMT)
        target_link_libraries(antebuffer fmt)
    endif()
    install(TARGETS antebuffer RUNTIME DESTINATION ${LIBSUFFIX}/utils)
    add_dependencies(utils_with_io antebuffer)
endif()

if(ENABLE_SHARED)
    install(TARGETS utils utils_with_io DESTINATION ${LIBSUFFIX}/utils)
endif()

add_executable(numbertheory_tool numbertheory_tool.cpp)
target_link_libraries (numbertheory_tool utils)

install(TARGETS numbertheory_tool RUNTIME DESTINATION ${LIBSUFFIX}/utils)

include(CMakeLists-nodist.txt OPTIONAL)

