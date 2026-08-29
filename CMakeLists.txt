cmake_minimum_required(VERSION 3.20)
project(video_segmenter C)
set(CMAKE_C_STANDARD 11)

add_executable(video_segmenter video_segmenter.c)
target_link_libraries(video_segmenter PRIVATE avformat avcodec avutil m)

add_executable(test_video_segmenter test_video_segmenter.c)
target_link_libraries(test_video_segmenter PRIVATE avformat avcodec avutil m)