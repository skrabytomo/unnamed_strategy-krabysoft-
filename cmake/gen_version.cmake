# Runs at BUILD time (via the gen_version custom target) to emit version_gen.h
# with the current git short-hash, branch and build date. Regenerating every
# build keeps the stamp fresh even on incremental builds (run.sh never
# reconfigures), and the "only rewrite if changed" guard means main.cpp is
# recompiled only when the commit/branch/date actually changes.
#
# Inputs (passed with -D): SRC = repo root, OUT = header path to write.
execute_process(COMMAND git -C "${SRC}" rev-parse --short HEAD
                OUTPUT_VARIABLE GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C "${SRC}" rev-parse --abbrev-ref HEAD
                OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C "${SRC}" status --porcelain
                OUTPUT_VARIABLE GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT GIT_HASH)
    set(GIT_HASH "nogit")
endif()
if(NOT GIT_BRANCH)
    set(GIT_BRANCH "?")
endif()
if(GIT_DIRTY)
    set(GIT_HASH "${GIT_HASH}-dirty")   # uncommitted changes present at build time
endif()
string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)

set(CONTENT "#pragma once\n")
string(APPEND CONTENT "#define GAME_GIT_HASH \"${GIT_HASH}\"\n")
string(APPEND CONTENT "#define GAME_GIT_BRANCH \"${GIT_BRANCH}\"\n")
string(APPEND CONTENT "#define GAME_BUILD_DATE \"${BUILD_DATE}\"\n")

if(EXISTS "${OUT}")
    file(READ "${OUT}" OLD)
else()
    set(OLD "")
endif()
if(NOT OLD STREQUAL CONTENT)
    file(WRITE "${OUT}" "${CONTENT}")
endif()
