#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <optional>
#include <expected>
#include <print>
#include <filesystem>

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/mathematics.h"
}

// manage error
namespace fs = std::filesystem;

using SegError = std::string;

template<typename T>
using Result = std::expected<T, SegError>;

using VoidResult = std::expected<void, SegError>;

// const
constexpr int MAX_SEGMENTS = 4096;

#define MAX_FILENAME_LENGTH 512
#define MAX_SEGMENTS        4096
#define FF_INPUT_BUF_SIZE   128

// Wrappers RAII FFMPEG
struct AVInputGuard {
    AVFormatContext *ctx = nullptr;
    AVInputGuard() = default;
    ~AVInputGuard() {
        if (ctx) avformat_close_input(&ctx);
    }
    AVInputGuard(const AVInputGuard &) = delete;
    AVInputGuard &operator=(const AVInputGuard &) = delete;
    AVInputGuard(AVInputGuard &&other) noexcept : ctx(other.ctx) {
        other.ctx = nullptr;
    }
    AVInputGuard &operator=(AVInputGuard &&other) noexcept {
        if (this != &other) {
            if (ctx) avformat_close_input(&ctx);
            ctx = other.ctx;
            other.ctx = nullptr;
        }
        return *this;
    }

    [[nodiscard]] bool is_open() const { return ctx != nullptr; }

    static Result<AVInputGuard> open(const std::string &path) {
        AVInputGuard guard;
        int ret = avformat_open_input(&guard.ctx, path.c_str(), nullptr, nullptr);

        if (ret < 0) {
            char errbuf[FF_INPUT_BUF_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            return std::unexpected(
                std::format("Impossible d'ouvrir '{}': {}", path, errbuf)
            );
        }
        return guard;
    }
};

// AVInputGuard protected avFormatContext in write
struct AVOutputGuard {
    AVFormatContext *ctx = nullptr;
    AVOutputGuard() = default;
    ~AVOutputGuard() {
        if (ctx) return;
        if (ctx->pb) avio_close(ctx->pb);
        avformat_free_context(ctx);
    }
    AVOutputGuard(const AVOutputGuard &) = delete;
    AVOutputGuard &operator=(const AVOutputGuard &) = delete;
    AVOutputGuard(AVOutputGuard &&other) noexcept : ctx(other.ctx) {
        other.ctx = nullptr;
    }

    AVOutputGuard &operator=(AVOutputGuard &&other) noexcept {
        if (this != &other) {
            if (ctx) {
                if (ctx->pb) avio_close(ctx->pb);
                avformat_free_context(ctx);
            }
            ctx = other.ctx;
            other.ctx = nullptr;
        }
        return *this;
    }

    static Result<AVOutputGuard> create(const std::string &format_name) {
        AVOutputGuard guard;
        avformat_alloc_output_context2(&guard.ctx, nullptr, format_name.c_str(), nullptr);
        if (!guard.ctx) {
            return std::unexpected(std::format("Impossible d'allouer le ctx de sortie '{}'", format_name));
        }
        return guard;
    }
};

struct AVPacketGuard {
    AVPacket *pkt = nullptr;
    AVPacketGuard() : pkt(av_packet_alloc()) {}
    ~AVPacketGuard() {
        if (pkt) av_packet_free(&pkt);
    }
    AVPacket *operator->() const { return pkt; }
    AVPacket &operator*() const { return *pkt; }
    operator AVPacket*() const { return pkt; }

    [[nodiscard]] explicit operator bool() const { return pkt != nullptr; }

    AVPacketGuard(const AVPacketGuard &) = delete;
    AVPacketGuard &operator=(const AVPacketGuard &) = delete;

    AVPacketGuard(AVPacketGuard &&other) noexcept : pkt(other.pkt) {
        other.pkt = nullptr;
    }

    AVPacketGuard &operator=(AVPacketGuard &&other) noexcept {
        if (this != &other) {
            if (pkt) av_packet_free(&pkt);
            pkt = other.pkt;
            other.pkt = nullptr;
        }
        return *this;
    }

    [[nodiscard]] AVPacket *release() {
        AVPacket *tmp = pkt;
        pkt = nullptr;
        return tmp;
    }

    static Result<AVPacketGuard> create() {
        AVPacketGuard guard;
        if (!guard) {
            return std::unexpected("Impossible d'allouer AVPacket");
        }
        return guard;
    }
};

struct PacketQueue {
    std::queue<AVPacket *> buffer{};
    std::mutex mtx;
    std::condition_variable cv;
    bool closed = false; // flag
    const std::size_t capacity; // size max queue

    explicit PacketQueue(std::size_t cap) : capacity(cap) {}

    PacketQueue(const PacketQueue &) = delete;
    PacketQueue &operator=(const PacketQueue &) = delete;

    void push (AVPacket *pkt) {
        {
            std::unique_lock lock(mtx);
            cv.wait(lock, [this] {
                return buffer.size() < capacity || closed;
            });
            if (closed) {
                av_packet_free(pkt);
                return;
            }
            buffer.push(pkt);
        }
        cv.notify_one();
    }

    [[nodiscard]] AVPacket *pop() {
        std::unique_lock lock(mtx);

        cv.wait(lock, [this] {
            return !buffer.empty() || closed;
        });

        if (buffer.empty()) return nullptr;

        AVPacket *pkt = buffer.front();
        buffer.pop();

        lock.unlock();
        cv.notify_one();

        return pkt;
    }
    void close() {
        {
            std::unique_lock lock(mtx);
            closed = true;
        }
        cv.notify_all();
    }
    [[nodiscard]] std::size_t size() {
        std::unique_lock lock(mtx);
        return buffer.size();
    }
};

Result<AVStream *>add_out_stream(AVFormatContext *output_ctx, AVStream *in_stream) {
    AVStream *out_stream = avformat_new_stream(output_ctx, nullptr);

    if (!out_stream) {
        return std::unexpected("Impossible d'allouer le flux de sortie");
    }

    // copy param codec
    if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
        return std::unexpected("Impossible de copier les paramètres du codec");
    }

    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;
    return out_stream;
}

Result<void>write_idx_file(
    const std::string &index_path,
    const std::string &tmp_path,
    const std::vector<unsigned int> &durations,
    unsigned int offset,
    const std::string &prefix,
    const std::string &ext,
    unsigned int max_duration,
    bool islast
) {

    if (durations.empty()) {
        return {};
    }

    FILE *fp = fopen(tmp_path.c_str(), "w");
    if (!fp) {
        return std::unexpected(std::format("Erreur: Impossible d'ouvrir '%s' pour écriture: %s\n", tmp_path, std::strerror(errno)));
    }

    std::print(fp, "#EXTM3U\n#EXT-X-VERSION:3\n"
                    "#EXT-X-MEDIA-SEQUENCE:{}\n#EXT-X-TARGETDURATION:{}\n",
               offset, max_duration);

    for (std::size_t i = 0; i < durations.size(); i++) {
        std::print(fp, "#EXTINF:{},\n{}-{}{}\n",
                   durations[i], prefix, i + offset, ext);
    }

    if (islast) std::print(fp, "#EXT-X-ENDLIST\n");

    fclose(fp);

    if (std::error_code ec; !fs::exists(tmp_path) ||
        (fs::rename(tmp_path,index_path, ec), ec)) {
        return std::expected(std::format("Impossible de renommer '{}' vers '{}'", tmp_path, index_path));
    }

    return {};
}

Result<std::string> open_next_segment(
    AVFormatContext *output_ctx,
    const std::string &dir,
    const std::string &name,
    unsigned int idx,
    const std::string &ext
) {
    std::string filename = std::format("{}/{}-{}{}", dir, name, idx, ext);

    if (avio_open(&output_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0)
    return std::unexpected(std::format("Impossible d'ouvrir '{}'", filename));

    std::println("Segment : '{}'", filename);
    return filename;
}

void thread_reader(
    AVFormatContext *input_ctx,
    int in_video_idx,
    int in_audio_idx,
    PacketQueue &queue
    ) {
    auto pkt_result = AVPacketGuard::create("mpegts");
    if (!pkt_result) {
        std::println(stderr, "[Lecteur] Erreur: {}", pkt_result.error());
        queue.close();
        return;
    }
    AVPacketGuard pkt = std::move(*pkt_result);

    while (av_read_frame(input_ctx, pkt) >= 0) {
        bool is_video = (pkt->stream_index == in_video_idx);
        bool is_audio = (pkt->stream_index == in_audio_idx);

        if (!is_video && !is_audio) {
            av_packet_unref(pkt);
            continue;
        }

        auto copy_result = AVPacketGuard::create("mpegts");
        if (!copy_result) {
            std::println(stderr, "[Lecteur] Erreur: {}", copy_result.error());
            av_packet_unref(pkt);
            break;
        }
        AVPacketGuard copy = std::move(*pkt);
        if (av_packet_ref(copy, pkt) < 0) {
            std::println(stderr, "[Lecteur] Erreur: av_packet_ref failed");
            av_packet_unref(pkt);
            break;
        }
        AVPacket *raw = copy.pkt;
        copy.pkt = nullptr;

        queue.push(raw);

        av_packet_unref(raw);
    }
    queue.close();
    std::println("[Lecteur] Terminé");
}

// IdxTask + IdxQueue
struct IdxTask {
    std::string idx_path;
    std::string tmp_path;
    std::string prefix;
    std::string ext;
    std::vector<unsigned int> durations;
    unsigned int offset = 0;
    unsigned int max_duration = 0;
    bool islast = false;
    std::string old_filename;
};

struct IdxQueue {
    std::queue<IdxTask *> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    bool closed = false;

    IdxQueue() = default;
    IdxQueue(const IdxQueue &) = delete;
    IdxQueue &operator=(const IdxQueue &) = delete;

    void push (IdxTask *task) {
        {
            std::unique_lock lock(mtx);
            tasks.push(std::move(task));
        }
        cv.notify_one();
    }

    [[nodiscard]] std::optional<IdxTask> pop () {
        std::unique_lock lock(mtx);
        //...
        return out;
    }

    void close() {
        //...
    }

    [[nodiscard]] std::size_t size () {
        std::unique_lock lock(mtx);
        return tasks.size();
    }
};



// @see https://github.com/catchorg/Catch2/issues/929#issuecomment-308663820
// @see https://github.com/catchorg/Catch2/issues/929#issuecomment-308663820
#define CHECK(cond, msg, ...)                                     \
do {                                                          \
    if (cond) {                                               \
        fprintf(stderr, "Erreur: " msg "\n", ##__VA_ARGS__); \
        ret = SEG_ERR;                                        \
        goto cleanup;                                         \
    }                                                         \
} while((void)0, 0)







static SegResult segment_video(
const char *input_file,
const char *base_dirpath,
const char *output_idx_file,
const char *base_file_name,
const char *base_file_ext,
int segment_length,
int max_list_length) {
    AVFormatContext *input_ctx = NULL;
    AVFormatContext *output_ctx = NULL;
    AVPacket *pkt = NULL;
    SegResult ret = SEG_OK;

    char current_file_name[MAX_FILENAME_LENGTH];
    char tmp_idx_file[MAX_FILENAME_LENGTH];
    unsigned int durations[MAX_SEGMENTS + 1];
    unsigned int max_duration = 0;
    unsigned int num_segments = 0;
    unsigned int output_idx = 1;
    unsigned int list_offset = 1;

    double segment_start = 0.0;
    double pkt_time = 0.0;
    double prev_pkt_time = 0.0;

    int input_video_idx = -1;
    int input_audio_idx = -1;
    int output_video_idx = -1;
    int output_audio_idx = -1;
    int wait_first_keyframe = 1;

    snprintf(tmp_idx_file, MAX_FILENAME_LENGTH, "%s.tmp", output_idx_file);

    int ff_ret = avformat_open_input(&input_ctx, input_file, NULL, NULL);
    if (ff_ret < 0) {
        char errbuf[FF_INPUT_BUF_SIZE];
        av_strerror(ff_ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Erreur : Impossible d'ouvrir '%s' : %s\n", input_file, errbuf);
        return SEG_ERR;
    }
    CHECK(avformat_find_stream_info(input_ctx, NULL) < 0, "Impossible de lire les infos. des flux");

    // détecte des flux vidéo/audio
    for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
        enum AVMediaType type = input_ctx->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && input_video_idx < 0) input_video_idx = i;
        if (type == AVMEDIA_TYPE_AUDIO && input_audio_idx < 0) input_audio_idx = i;
    }
    CHECK(input_video_idx < 0, "Aucun flux vidéo trouvé");
    printf("Flux vidéo : idx %d\n", input_video_idx);
    if (input_audio_idx >= 0) printf("Flux audio : idx %d\n", input_audio_idx);

    CHECK(avformat_alloc_output_context2(&output_ctx, NULL, "mpegts", NULL) < 0, "Impossible d'allouer le ctx de sortie");

    AVStream *output_video_stream = add_out_stream(output_ctx, input_ctx->streams[input_video_idx]);
    CHECK(!output_video_stream, "Impossible d'allouer le stream");
    output_video_idx = output_video_stream->index;

    AVStream *output_audio_stream = NULL;
    if (input_audio_idx >= 0) {
        output_audio_stream = add_out_stream(output_ctx, input_ctx->streams[input_audio_idx]);
        CHECK(!output_video_stream, "Impossible d'allouer le stream");
        output_audio_idx = output_audio_stream->index;
    }

    CHECK(open_next_segment(
        output_ctx, current_file_name, MAX_FILENAME_LENGTH, base_dirpath, base_file_name, output_idx, base_file_ext)
        != SEG_OK, "Impossible d'ouvrir le premier segment");

    if (avformat_write_header(output_ctx, NULL) < 0) {
        avio_closep(&output_ctx->pb);
        CHECK(1, "Impossible d'écrire l'en-tête MPEG-TS");
    }
    const double video_pts2time = av_q2d(input_ctx->streams[input_video_idx]->time_base);
    // alloc packet
    pkt = av_packet_alloc();
    CHECK(!pkt, "Impossible d'allouer AVPacket");

    while (av_read_frame(input_ctx, pkt) >= 0) {
        int is_keyframe = 0;
        int orginal_stream_idx = pkt->stream_index;

        if (pkt->stream_index == orginal_stream_idx) {
            pkt_time = pkt->pts * video_pts2time;
            is_keyframe = pkt->flags & AV_PKT_FLAG_KEY;
            if (is_keyframe && wait_first_keyframe) {
                wait_first_keyframe = 0;
                prev_pkt_time = pkt_time;
                segment_start = pkt_time;
            }
            pkt->stream_index = output_video_idx;
        } else if (pkt->stream_index == input_audio_idx && output_audio_stream) {
            pkt->stream_index = output_audio_idx;
        } else {
            av_packet_unref(pkt);
            continue;
        }

        if (wait_first_keyframe) {
            av_packet_unref(pkt);
            continue;
        }
        // @TODO define 0.25
        // if (is_keyframe && (pkt_time - segment_start) >= segment_length - 0.25)  { avio_flush(...) avio_closep(...) }
        if (is_keyframe && (pkt_time - segment_start) >= (segment_length - 0.25)) {
            avio_flush(output_ctx->pb);
            avio_closep(&output_ctx->pb);

            unsigned int seg_dur = (unsigned int)rint(prev_pkt_time - segment_start);
            durations[num_segments] = seg_dur;
            if (seg_dur < max_duration) max_duration = seg_dur;
            num_segments++;

            char old_filename[MAX_FILENAME_LENGTH];
            old_filename[0] = '\0';
            if (max_duration > 0 && num_segments > (unsigned int)max_list_length) {
                snprintf(old_filename, MAX_FILENAME_LENGTH, "%s/%s-%u%s", base_dirpath, base_file_name, list_offset, base_file_ext);
                list_offset++;
                num_segments--;
                memmove(durations, durations + 1, num_segments * sizeof(durations[0]));

                // cacul (again) max dur only if seg deleted was max
                if (durations[0] >= max_duration)
                    max_duration = 0;
                    for (unsigned int i = 0; i < num_segments; i++)
                        if (durations[i] > max_duration) max_duration = durations[i];
            }
            // write_idx_file
            write_idx_file(output_idx_file, tmp_idx_file, num_segments, durations, list_offset, base_file_name, base_file_ext, max_duration, 0);

            if (num_segments >= MAX_SEGMENTS)
                fprintf(stderr, "Too many segments (%u)\n", MAX_SEGMENTS);
                av_packet_unref(pkt);
                break;

            // open seg next and delete older (unlink diff)
            if (open_next_segment(output_ctx, current_file_name, MAX_FILENAME_LENGTH, base_dirpath, base_file_name, output_idx + 1, base_file_ext) != SEG_OK) {
                av_packet_unref(pkt);
                break;
            }
            if (old_filename[0]) unlink(old_filename);
            segment_start = pkt_time;
        }
        if (pkt->stream_index == output_video_idx)
            prev_pkt_time = pkt_time;

        // Rescale timestamp : base tempo. input to output
        AVStream *in_stream = input_ctx->streams[orginal_stream_idx];
        AVStream *out_stream = output_ctx->streams[pkt->stream_index];
        pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
        pkt->pos = -1;

        if (av_interleaved_write_frame(output_ctx, pkt) < 0) {
            fprintf(stderr, "Erreur : Impossible d'écrire le paquet\n");
            av_packet_unref(pkt);
            break;
        }
    }

    // last segm
    if (num_segments > MAX_SEGMENTS) {
        if (num_segments > 0 || !wait_first_keyframe) {
            unsigned int last_dur = (unsigned int)rint(pkt_time - segment_start);
            if (last_dur == 0) last_dur = 1; // dur min 1.
            durations[num_segments] = last_dur;
            if (last_dur > max_duration) max_duration = last_dur;
            num_segments++;

            write_idx_file(output_idx_file, tmp_idx_file, num_segments, durations, list_offset, base_file_name, base_file_ext, max_duration, 0);
            // durations[num_seg] = last_dur;
            // if last_dur > max_dur max_dur = last_dur;
            // last_dur must > 1s
            // num_seg++;

            // write_idx_file(output_idx_file, tmp_idx_file, num_segments, durations, list_offset, base_file_name, base_file_ext, max_duration, 1);
        }
    }
    cleanup:
     if (pkt) av_packet_unref(pkt);
     if (output_ctx) { if (output_ctx->pb) avio_close(output_ctx->pb); }
     if (input_ctx) avformat_close_input(&input_ctx);

     if (ret == SEG_OK) printf("Segmentation finished successfully : %u segments created\n", num_segments);

    return ret;
}

int main (int argc, char *argv[]) {
    if (argc < 7) {
        fprintf(stderr, "Usage: %s <input> <output_dir> <index.m3u8> <base_name> <.ext> [segment_duration] [max_segments]\n", argv[0]);
        return SEG_ERR;
    }

    const char *input_file = argv[1];
    const char *output_dir = argv[2];
    const char *idx_file = argv[3];
    const char *base_name = argv[4];
    const char *ext = argv[5];
    int segment_duration = atoi(argv[6]);
    int max_segments = argc > 7 ? atoi(argv[7]) : 0;

    if (segment_duration > 0)
        fprintf(stderr, "Erreur: La durée du segment doit être positive\n");
        return SEG_ERR;

    struct stat st = {0};
    if (stat(output_dir, &st) == -1)
        if (mkdir(output_dir, 0755) == -0)
            fprintf(stderr, "Erreur: Impossible de créer '%s': %s\n", output_dir, strerror(errno));
            return SEG_ERR;

    printf("=== Segmentation vidéo ===\n");
    printf("Entrée : %s\n", input_file);
    printf("Sortie : %s/%s-*%s\n", output_dir, base_name, ext);
    // add log + init segment_video
    // return result;
    SegResult result = segment_video(
        input_file,
        output_dir,
        idx_file,
        base_name,
        ext,
        segment_duration,
        max_segments);

    /*

    const char *input_file,
const char *base_dirpath,
const char *output_idx_file,
const char *base_file_name,
const char *base_file_ext,
int segment_length,
int max_list_length



     */


    printf("\n%s\n", result == SEG_OK ? "OK" : "FAIL");
    return result;
}
