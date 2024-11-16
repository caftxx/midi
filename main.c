#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <endian.h>

#define MIDI_HEADER_MAGIC       0x6468544d
#define MIDI_TRACK_HEADER_MAGIC 0x6b72544d

#define MIDI_OK     0
#define MIDI_AGAIN  -1
#define MIDI_ABORT  -0xFF

#define BUF_SIZE                32
#define MIDI_HEADER_LEN         14U
#define MIDI_TRACK_HEADER_LEN   8U

#define LOG_ERROR(fmt, ...) do {fprintf(stderr, "%s:%d -- "fmt"\n", __FILE__, __LINE__, ##__VA_ARGS__);} while (0)
#define LOG_INFO(fmt, ...) do {fprintf(stdout, "%s:%d -- "fmt"\n", __FILE__, __LINE__, ##__VA_ARGS__);} while (0)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// Constants for the MIDI channel events, first nibble
#define NOTE_OFF 0x80
#define NOTE_ON 0x90
#define POLYTOUCH 0xa0
#define CONTROL_CHANGE 0xb0
#define PROGRAM_CHANGE 0xc0
#define AFTERTOUCH 0xd0
#define PITCHWHEEL 0xe0
#define _FIRST_CHANNEL_EVENT 0x80
#define _LAST_CHANNEL_EVENT 0xef

// Most midi channel events have 2 bytes of data, except this range that has 1 byte events
// which consists of two event types:
#define _FIRST_1BYTE_EVENT 0xc0
#define _LAST_1BYTE_EVENT 0xdf

// Meta messages
#define _META_PREFIX 0xff

// Meta messages, second byte
#define SEQUENCE_NUMBER 0x00
#define TEXT 0x01
#define COPYRIGHT 0x02
#define TRACK_NAME 0x03
#define INSTRUMENT_NAME 0x04
#define LYRICS 0x05
#define MARKER 0x06
#define CUE_MARKER 0x07
#define PROGRAM_NAME 0x08
#define DEVICE_NAME 0x09
#define CHANNEL_PREFIX 0x20
#define MIDI_PORT 0x21
#define END_OF_TRACK 0x2f
#define SET_TEMPO 0x51
#define SMPTE_OFFSET 0x54
#define TIME_SIGNATURE 0x58
#define KEY_SIGNATURE 0x59
#define SEQUENCER_SPECIFIC 0x7f
#define _FIRST_META_EVENT 0x00
#define _LAST_META_EVENT 0x7f

// Sysex/escape events
#define SYSEX 0xf0
#define ESCAPE 0xf7

typedef enum {
    DECODE_HEADER = 0,
    DECODE_TRACK_HEADER,
    DECODE_TRACK_EVENT_DELTA,
    DECODE_TRACK_EVENT_STATUS,
    DECODE_TRACK_EVENT_PARAM1,
    DECODE_TRACK_EVENT_PARAM2,
    DECODE_TRACK_EVENT_NON_CHANNEL,
    DECODE_TRACK_EVENT_DROP,
    DECODE_COMPLETE
} decode_status_t;
typedef struct {
    uint32_t magic; // MThd
    uint32_t len; // always is 6
    short format; // 0: single Mtrk chunks sync; 1: two or more MTrk chunks sync; 2: multi MTrk chunks async
    short num_tracks; // number of chunks
    short ticks_per_quarter; // pulses per beat
} midi_header_t;
typedef struct {
    uint32_t magic;
    uint32_t len;
} midi_track_t;
typedef struct {
    int delta;
    uint8_t status;
    uint8_t param1;
    uint8_t param2;
} midi_event_t;
typedef struct {
    midi_header_t header;
    midi_track_t track;
    int handle_track_len;
    int handle_tracks_count;

    decode_status_t status;
    uint8_t last_event_status_avail;
    uint8_t last_event_status;

    midi_event_t event;

    union {
        struct {
            uint8_t buf_off;
            char buf[BUF_SIZE];
        };
        struct {
            int total_len;
            int drop_len;
        };
        int value;
    } tmp;
} midi_context_t;

int midi_number(uint8_t *buf, int len, int *value)
{
    uint8_t *ptr = buf;

    *value = (*value << 7) | (*ptr & 0x7f);
    while (*ptr >= 0x80) {
        if (++ptr >= (buf+len)) {
            break;
        }
        *value = (*value << 7) | (*ptr & 0x7f);
    }

end:
    return ptr < (buf+len) ? MIDI_OK : MIDI_AGAIN;
}

int midi_process_header(midi_context_t *ctx)
{
    int ret = MIDI_OK;

    if (ctx->header.magic != MIDI_HEADER_MAGIC) {
        LOG_ERROR("invalid midi header magic:0x%x", ctx->header.magic);
        ret = MIDI_ABORT;
        goto end;
    }

    ctx->header.len = be32toh(ctx->header.len);
    ctx->header.format = be16toh(ctx->header.format);
    ctx->header.ticks_per_quarter = be16toh(ctx->header.ticks_per_quarter);
    ctx->header.num_tracks = be16toh(ctx->header.num_tracks);

    ctx->status = DECODE_TRACK_HEADER;

end:
    return ret;
}

int midi_process_track(midi_context_t *ctx)
{
    int ret = MIDI_OK;
    int value = 0;

    midi_track_t *track = &ctx->track;
    if (track->magic != MIDI_TRACK_HEADER_MAGIC) {
        LOG_ERROR("invalid midi track header magic:0x%x", track->magic);
        ret = MIDI_ABORT;
        goto end;
    }

    track->len = be32toh(track->len);

    ctx->status = DECODE_TRACK_EVENT_DELTA;

end:
    return ret;
}

int midi_process_event(midi_context_t *ctx)
{
    int ret = MIDI_OK;

    if (ctx->event.status >= _FIRST_CHANNEL_EVENT && ctx->event.status <= _LAST_CHANNEL_EVENT) {
        ctx->last_event_status = ctx->event.status;
        ctx->last_event_status_avail = 1;
        ctx->status = DECODE_TRACK_EVENT_PARAM1;
    } else if (ctx->event.status == _META_PREFIX
                || ctx->event.status == SYSEX
                || ctx->event.status == ESCAPE) {
        ctx->status = DECODE_TRACK_EVENT_NON_CHANNEL;
    } else {
        LOG_ERROR("unsupport event status:0x%x", ctx->event.status);
        ret = MIDI_ABORT;
    }

    return ret;
}

void midi_dump_event(midi_context_t *ctx)
{
    printf("delta:%d, status:0x%x, param1:0x%x, param2:0x%x\n",
            ctx->event.delta, ctx->event.status, ctx->event.param1, ctx->event.param2);
}

int midi_decode(midi_context_t *ctx, uint8_t *buf, int len)
{
    int ret = 0;
    int done = 0;

    switch (ctx->status) {
        case DECODE_HEADER: {
            ret = MIN(MIDI_HEADER_LEN - ctx->tmp.buf_off, len);
            memcpy(&ctx->tmp.buf[ctx->tmp.buf_off], buf, ret);
            ctx->tmp.buf_off += ret;
            len -= ret;
            if (ctx->tmp.buf_off < MIDI_HEADER_LEN) {
                break;
            }
            memcpy(&ctx->header, ctx->tmp.buf, MIDI_HEADER_LEN);
            int _ret = midi_process_header(ctx);
            if (_ret != MIDI_OK) {
                ret = -1;
            }
            done = 1;
        } break;
        case DECODE_TRACK_HEADER: {
            ret = MIN(MIDI_TRACK_HEADER_LEN - ctx->tmp.buf_off, len);
            memcpy(&ctx->tmp.buf[ctx->tmp.buf_off], buf, ret);
            ctx->tmp.buf_off += ret;
            len -= ret;
            if (ctx->tmp.buf_off < MIDI_TRACK_HEADER_LEN) {
                break;
            }
            ctx->track.magic = *(uint32_t *)(ctx->tmp.buf);
            ctx->track.len = *(uint32_t *)(ctx->tmp.buf + 4);
            ctx->handle_track_len = 0;
            int _ret = midi_process_track(ctx);
            if (_ret != MIDI_OK) {
                ret = -1;
            }
            done = 1;
        } break;
        case DECODE_TRACK_EVENT_DELTA: {
            if (ctx->handle_track_len == ctx->track.len) {
                ctx->handle_tracks_count += 1;
                if (ctx->handle_tracks_count == ctx->header.num_tracks) {
                    ctx->status = DECODE_COMPLETE;
                } else {
                    ctx->status = DECODE_TRACK_HEADER;
                }
                break;
            }
            ret = midi_number(buf, len, &ctx->tmp.value);
            if (ret != MIDI_OK) {
                ret = len;
                break;
            }
            ret = 0;
            while (buf[ret++] >= 0x80) ;
            ctx->event.delta = ctx->tmp.value;
            ctx->status = DECODE_TRACK_EVENT_STATUS;
            done = 1;
        } break;
        case DECODE_TRACK_EVENT_STATUS: {
            if (buf[0] < 0x80) {
                if (!ctx->last_event_status_avail) {
                    ret = -1;
                    LOG_ERROR("event status not found:0x%x", buf[0]);
                    break;
                }
                ctx->event.status = ctx->last_event_status;
            } else {
                ctx->event.status = buf[0];
                ret = 1;
            }
            int _ret = midi_process_event(ctx);
            if (_ret != MIDI_OK) {
                ret = -1;
                break;
            }
            done = 1;
        } break;
        case DECODE_TRACK_EVENT_PARAM1: {
            ctx->event.param1 = buf[0];
            if (ctx->event.status >= _FIRST_1BYTE_EVENT && ctx->event.status <= _LAST_1BYTE_EVENT) {
                ctx->status = DECODE_TRACK_EVENT_DELTA;
                midi_dump_event(ctx);
            } else {
                ctx->status = DECODE_TRACK_EVENT_PARAM2;
            }
            ret = 1;
            done = 1;
        } break;
        case DECODE_TRACK_EVENT_PARAM2: {
            ctx->event.param2 = buf[0];
            ctx->status = DECODE_TRACK_EVENT_DELTA;
            midi_dump_event(ctx);
            ret = 1;
            done = 1;
        } break;
        case DECODE_TRACK_EVENT_NON_CHANNEL: {
            if (ctx->event.status == _META_PREFIX) {
                ctx->event.status = buf[0];
                ret = 1;
                if (ctx->event.status < _FIRST_META_EVENT || ctx->event.status > _LAST_META_EVENT) {
                    LOG_ERROR("invalid midi meta second event status:0x%x, not in range 0x00-0x7f", ctx->event.status);
                    ret = -1;
                }
                break;
            }
            ret = midi_number(buf, len, &ctx->tmp.total_len);
            if (ret != MIDI_OK) {
                ret = len;
                break;
            }
            ret = 0;
            while (buf[ret++] >= 0x80) ;
            ctx->status = DECODE_TRACK_EVENT_DROP;
        } break;
        case DECODE_TRACK_EVENT_DROP: {
            ret = MIN(ctx->tmp.total_len - ctx->tmp.drop_len, len);
            ctx->tmp.drop_len += ret;
            if (ctx->tmp.drop_len == ctx->tmp.total_len) {
                ctx->status = DECODE_TRACK_EVENT_DELTA;
                done = 1;
            }
        } break;
        case DECODE_COMPLETE: {
            ret = len;
            done = 1;
        } break;
        default: {
            LOG_ERROR("invalid decode status:%d", ctx->status);
            ret = -1;
        } break;
    }

    ctx->handle_track_len += ret;

    if (done) {
        memset(&ctx->tmp, 0, sizeof(ctx->tmp));
    }

    return ret;
}

int main(int argc, void **argv)
{
    if (argc < 2) {
        LOG_ERROR("missing midi file");
        return -1;
    }

    const char *fpath = argv[1];
    int fd = open(fpath, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("fail to open fpath:%s", fpath);
        return -1;
    }

    midi_context_t ctx = {0};
    char buf[BUF_SIZE] = {0};
    int ret = 0;
    while (1) {;
        ret = read(fd, buf, sizeof(buf));
        if (ret == 0) {
            break;
        } else if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            LOG_ERROR("some error happen, errno:%d", errno);
            break;
        }

        int len = ret;
        int off = 0;
        do {
            ret = midi_decode(&ctx, buf + off, len);
            if (ret > 0) {
                len -= ret;
                off += ret;
            }
        } while (ret >= 0 && len > 0);
        
        if (ret < 0) {
            break;
        }
    }

    assert(ctx.status == DECODE_COMPLETE);
    close(fd);
    return ret;
}
