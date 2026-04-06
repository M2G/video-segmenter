#!/bin/bash

echo "Watching project..."

fswatch -o *.c *.h | while read
do
    clear
    echo "Recompiling..."

    gcc *.c -o program $(pkg-config --cflags --libs libavformat libavcodec libavutil)

    if [ $? -eq 0 ]; then
        echo "Build OK"
    else
        echo "Build Error"
    fi
done