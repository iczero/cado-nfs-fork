set (SUF_SRC ${CADO_NFS_SOURCE_DIR}/sieve/strategies)

set (SUF_UTILS ${SUF_SRC}/utils_st/)
set (SRC_UTILS ${SUF_UTILS}point.c ${SUF_UTILS}tab_point.c
  ${SUF_UTILS}convex_hull.c ${SUF_UTILS}fm.c ${SUF_UTILS}tab_fm.cpp
  ${SUF_UTILS}strategy.c ${SUF_UTILS}tab_strategy.c
  ${SUF_UTILS}decomp.c ${SUF_UTILS}tab_decomp.c)

set (LIB_EXEC m facul utils)

include_directories(${CADO_NFS_SOURCE_DIR})
include_directories(${CADO_NFS_SOURCE_DIR}/sieve)
include_directories(${CADO_NFS_SOURCE_DIR}/sieve/ecm)
include_directories(${CADO_NFS_SOURCE_DIR}/sieve/strategies)
include_directories(${CADO_NFS_SOURCE_DIR}/sieve/strategies/utils_st)

if (HAVE_REGEX_H)
cado_define_test(test_generate_composite_numbers.cpp
  ${SUF_SRC}/generate_factoring_method.cpp ${SRC_UTILS}
  LIBRARIES ${LIB_EXEC})

cado_define_test(test_generate_strategies.cpp
  ${SUF_SRC}/generate_factoring_method.cpp
  ${SUF_SRC}/generate_strategies.cpp
  ${SRC_UTILS}
  LIBRARIES ${LIB_EXEC})

cado_define_test(test_filtering.cpp
  ${SUF_SRC}/generate_factoring_method.cpp
  ${SRC_UTILS}
  LIBRARIES ${LIB_EXEC})

cado_define_test(test_benchfm.cpp
  ${SUF_SRC}/generate_factoring_method.cpp
  ${SRC_UTILS}
  LIBRARIES ${LIB_EXEC})

if(DEFINED ENV{CHECKS_EXPENSIVE})
    cado_define_test(SCRIPT
        ${CMAKE_CURRENT_SOURCE_DIR}/test_main_script.sh
        TARGET_DEPENDENCIES gfm benchfm gst finalst las makefb)
endif()
endif()

add_subdirectory (utils_st)
