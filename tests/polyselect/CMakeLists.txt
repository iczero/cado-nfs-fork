# Build polyselect before running check

include_directories(${PROJECT_SOURCE_DIR}/polyselect)

macro(polyselect_test n)
    cado_define_test(test_polsel_${n}
        PROGRAM ${CADO_NFS_BINARY_DIR}/polyselect/polyselect ${ARGN}
        TARGET_DEPENDENCIES polyselect)
endmacro()
set(example_N 90377629292003121684002147101760858109247336549001090677693)
polyselect_test(2 -N ${example_N} degree=2 P=1000 admax=100  nq=8)
polyselect_test(3 -N ${example_N} degree=3 P=1000 admax=500  nq=10)
polyselect_test(4 -N ${example_N} degree=4 P=1000 admax=1000 nq=10)
polyselect_test(5 -N ${example_N} degree=5 P=1000 admax=1000 nq=10)
polyselect_test(6 -N ${example_N} degree=6 P=1000 admax=1000 nq=10)
polyselect_test(7 -N ${example_N} degree=7 P=1000 admax=1000 nq=10 TIMEOUT 60)

if(DEFINED ENV{CHECKS_EXPENSIVE})
# in the following example, there are 6 roots for q=241, but only 0 or 2 roots
# for the other small primes
set(example_N 412023436986659543855531365332575948179811699844327982845455626433876445565248426198098870423161841879261420247188869492560931776375033421130982397485150944909106910269861031862704114880866970564902903653658867433731720813104105190864254793282601391257624033946373269391)
polyselect_test(8 -N ${example_N} degree=6 P=1000000 admin=3720
    admax=3721 nq=7776 TIMEOUT 60)
endif()

## we disable the following test since twocubics is not in "make dist"
## test also the twocubics binary
# cado_define_test(twocubics PROGRAM
#    ${CADO_NFS_BINARY_DIR}/polyselect/twocubics -n 999073468111577057576445816581 -P 500 -incr 30 -admax 1000
#    TARGET_DEPENDENCIES twocubics)

cado_define_test(test_auxiliary.c LIBRARIES utils tests polyselect_common)
cado_define_test(test_rho.c LIBRARIES polyselect_common tests)
# This one is more expensive than a few others, as it takes 5 seconds on
# a laptop, and can easily take tens of seconds on a container that runs
# on a busy machine.
cado_define_test(test_polyselect_shash.cpp
    ${PROJECT_SOURCE_DIR}/polyselect/polyselect_shash.c
    ${PROJECT_SOURCE_DIR}/polyselect/polyselect_poly_header.c
    ${PROJECT_SOURCE_DIR}/polyselect/polyselect_match.c
    LIBRARIES polyselect_common tests
    TIMEOUT 90
)

macro(define_sopt_test n input sha)
cado_define_test(test_sopt_${input} PROGRAM
    ${CADO_NFS_SOURCE_DIR}/tests/wrap-test.sh --expect-sha1
    ${sha} --filter-output "^[^#]" --
    ${CADO_NFS_BINARY_DIR}/polyselect/sopt  -sopteffort 2
    -inputpolys ${CMAKE_CURRENT_SOURCE_DIR}/${input}
    TARGET_DEPENDENCIES sopt
)
endmacro()

define_sopt_test(1 sopt.input1.txt 465209edaa2c8bbad4e7254a7d6fcf2f121a71b8)
define_sopt_test(2 sopt.input2.txt a3aadd110944689565adb70ae7456b69d8d97dbc)


cado_define_test(gfpkdlpolyselect_test.c
    ${CADO_NFS_SOURCE_DIR}/polyselect/gfpkdlpolyselect.c
    NO_DEFAULT_RUN
    LIBRARIES polyselect_common utils)

foreach(p 10000000019 10000000033 10000000061 10000000069 10000000097)
    cado_divert_test(gfpkdlpolyselect_test ${p} APPEND_ARGUMENTS -k 2 -p ${p} -label test${p})
endforeach()

cado_define_test(polyselect_add_score
    SCRIPT
    ${CADO_NFS_SOURCE_DIR}/tests/wrap-test.sh
        --expect-sha1 27954264d0ab7e6bf8bbc07a8376ff9c63649ba8
        --
        ${CADO_NFS_BINARY_DIR}/polyselect/score
        -Bf 1e+04 -Bg 2e+05 -area 5e+10
        ${CADO_NFS_SOURCE_DIR}/tests/misc/c60.poly
    TARGET_DEPENDENCIES score)

cado_define_test(polyselect_add_skewness
    SCRIPT
    ${CADO_NFS_SOURCE_DIR}/tests/wrap-test.sh
        --expect-sha1
        0694420c819d125950c304e205c0593cefa889db
        --
        ${CADO_NFS_BINARY_DIR}/polyselect/skewness
        ${CADO_NFS_SOURCE_DIR}/tests/misc/c60.poly
    TARGET_DEPENDENCIES skewness)

