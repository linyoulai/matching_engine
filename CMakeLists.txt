cmake_minimum_required(VERSION 3.10)

# 项目名称
project(MatchingEngine)

# 1. 关键：开启 C++17 标准，解决 std::shared_mutex 报错
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. 自动搜索并包含头文件目录
# 这样你在代码里写 #include "Gateway.h" 或 #include "httplib.h" 都能找得到
include_directories(lib/spdlog/include)
include_directories(include)
include_directories(lib)

# 3. 查找线程库 (Linux 环境下必须链接 pthread)
find_package(Threads REQUIRED)

# 4. 如果你下载了 spdlog 并放在了 lib 下，或者通过系统安装
# 如果是 header-only 版本，include_directories(lib) 已经足够
# 如果是编译版本，通常需要 find_package(spdlog REQUIRED)

# 5. 指定源文件
# 建议把 Gateway 的实现写在 src/Gateway.cpp 中
set(SOURCES 
    src/main.cpp
    src/Gateway.cpp
    src/MatchingEngine.cpp
    src/OrderBook.cpp
)

# 6. 生成可执行文件
add_executable(main ${SOURCES})

# 7. 链接库
# 链接线程库解决 std::thread 相关的链接问题
target_link_libraries(main PRIVATE Threads::Threads)

# 如果 spdlog 需要链接库（非 header-only），则添加：
# target_link_libraries(main PRIVATE spdlog::spdlog)