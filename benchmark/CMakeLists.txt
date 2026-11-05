# Command line benchmarks. They have no GUI, so they can run under perf.
# Enable with -DBUILD_BENCHMARKS=ON.
if(NOT WIN32)
  add_executable(tsm_bench tsm_bench.c)
  target_link_libraries(tsm_bench PRIVATE tsm)
endif()
