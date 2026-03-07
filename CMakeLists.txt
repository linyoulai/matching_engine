cmake_minimum_required(VERSION 3.10)
project(MatchingEngine)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

file(GLOB SOURCES "src/*.cpp")

add_executable(matching_engine ${SOURCES})

# ---- spdlog integration ----
include(FetchContent)
FetchContent_Declare(
	spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)
target_link_libraries(matching_engine PRIVATE spdlog::spdlog)
