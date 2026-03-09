cmake_minimum_required(VERSION 3.10)
project(rmp-leaderboard)

set(CMAKE_CXX_STANDARD 17)

find_package(CURL REQUIRED)

add_executable(rmp-leaderboard
    main.cpp
)

target_link_libraries(rmp-leaderboard CURL::libcurl)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)
target_link_libraries(rmp-leaderboard nlohmann_json::nlohmann_json)
