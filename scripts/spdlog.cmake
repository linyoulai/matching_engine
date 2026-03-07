# spdlog: Fast C++ logging library.
# https://github.com/gabime/spdlog

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

target_link_libraries(matching_engine PRIVATE spdlog::spdlog)
