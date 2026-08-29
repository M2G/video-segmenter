FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt video_segmenter.c test_video_segmenter.c ./
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target video_segmenter


FROM debian:bookworm-slim AS release

RUN apt-get update && apt-get install -y \
    bash \
    ffmpeg \
    libavcodec59 \
    libavformat59 \
    libavutil57 \
    libswscale6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /src/build/video_segmenter /app/usr/local/bin/video_segmenter
COPY video_processor.sh /app/video_processor.sh

RUN chmod +x /app/video_processor.sh /app/usr/local/bin/video_segmenter

ENTRYPOINT ["/app/video_processor.sh", "watch"]