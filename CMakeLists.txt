cmake_minimum_required(VERSION 3.20)
project(video_segmenter C)

set(CMAKE_C_STANDARD 11)

add_executable(video_segmenter video_segmenter.c)

target_include_directories(video_segmenter PRIVATE /opt/homebrew/include)
target_link_directories(video_segmenter PRIVATE /opt/homebrew/lib)

target_link_libraries(video_segmenter PRIVATE avformat avcodec avutil m)