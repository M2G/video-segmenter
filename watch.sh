#!/bin/bash

echo "Surveillance de video_segmenter.c..."
echo "    Ctrl+C pour arrêter"
echo ""

fswatch -o video_segmenter.c | while read; do
    echo "Changement détecté - compilation..."
    if make; then
        echo "Compilation réussie"
    else
        echo "Erreur de compilation"
    fi
    echo ""
done