#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <endian.h>

#include "midi.h"

int midi_number(uint8_t *buf, int *len, int *value);
int midi_decode_complete(midi_context_t *ctx, uint8_t *buf, int *len);
int midi_decode_event_drop(midi_context_t *ctx, uint8_t *buf, int *len);
int midi_decode_event_non_channel(midi_context_t *ctx, uint8_t *buf, int *len);
int midi_decode_event_param2(midi_context_t *ctx, uint8_t *buf, int *len);
int midi_decode_event_param1(midi_context_t *ctx, uint8_t *buf, int *len);
int midi_decode_event_status(midi_context_t *ctx, uint8_t *buf, int *len);
int midi_decode_event_delta(midi_context_t *ctx, uint8_t *buf, int *len);
int midi_decode_track_header(midi_context_t *ctx, uint8_t *buf, int *len);
int midi_decode_header(midi_context_t *ctx, uint8_t *buf, int *len);

double midi_note_to_freq(uint8_t note)
{
    double n = note;
    return 440 * pow(2, (n-69)/12);
}

void midi_process_event(midi_context_t *ctx, midi_event_t *event)
{
    if (ctx->tempo == 0) {
        // Start with default "microseconds per quarter" according to midi standard
        ctx->tempo = 500000;
    }

    // according to MIDI spec
    // time (in ms) = number_of_ticks * tempo / divisor * 1000
    // where tempo is expressed in microseconds per quarter note and
    // the divisor is expressed in MIDI ticks per quarter note
    // Do the math in microseconds.
    // Do not use floating point, in some microcontrollers
    // floating point is slow or lacks precision.
    event->delta = (event->delta * ctx->tempo + (ctx->header.ticks_per_quarter / 2)) / ctx->header.ticks_per_quarter;

    if (ctx->on_event) {
        ctx->on_event(ctx, &ctx->track.event);
    }
}

int midi_number(uint8_t *buf, int *len, int *value)
{
    int ret = MIDI_OK;
    int eat_len = 1;
    uint8_t *p = buf;

    for (; p < (buf + *len); ++p, ++eat_len) {
        *value = (*value << 7) | (*p & 0x7f);
        if (*p < 0x80) {
            break;
        }
    }

    if (p == (buf + *len)) {
        eat_len -= 1;
        ret = MIDI_AGAIN;
    }

    *len = eat_len;
    return ret;
}

int midi_decode_header(midi_context_t *ctx, uint8_t *buf, int *len)
{
    int eat_len = MIN(MIDI_HEADER_LEN - ctx->tmp.buf_off, *len);
    memcpy(&ctx->tmp.buf[ctx->tmp.buf_off], buf, eat_len);
    ctx->tmp.buf_off += eat_len;
    *len = eat_len;
    if (ctx->tmp.buf_off < MIDI_HEADER_LEN) {
        return MIDI_AGAIN;
    }

    memcpy(&ctx->header, ctx->tmp.buf, MIDI_HEADER_LEN);

    if (ctx->header.magic != MIDI_HEADER_MAGIC) {
        LOG_ERROR("invalid midi header magic:0x%x", ctx->header.magic);
        return MIDI_ABORT;
    }

    ctx->header.len = be32toh(ctx->header.len);
    ctx->header.format = be16toh(ctx->header.format);
    ctx->header.ticks_per_quarter = be16toh(ctx->header.ticks_per_quarter);
    ctx->header.num_tracks = be16toh(ctx->header.num_tracks);

    ctx->status = DECODE_TRACK_HEADER;

    return MIDI_OK;
}

int midi_decode_track_header(midi_context_t *ctx, uint8_t *buf, int *len)
{
    int eat_len = MIN(MIDI_TRACK_HEADER_LEN - ctx->tmp.buf_off, *len);
    memcpy(&ctx->tmp.buf[ctx->tmp.buf_off], buf, eat_len);
    ctx->tmp.buf_off += eat_len;
    *len = eat_len;
    if (ctx->tmp.buf_off < MIDI_TRACK_HEADER_LEN) {
        return MIDI_AGAIN;
    }

    midi_track_t *track = &ctx->track;
    track->magic = *(uint32_t *)(ctx->tmp.buf);
    track->len = *(uint32_t *)(ctx->tmp.buf + 4);

    if (track->magic != MIDI_TRACK_HEADER_MAGIC) {
        LOG_ERROR("invalid midi track header magic:0x%x", track->magic);
        return MIDI_ABORT;
    }

    track->len = be32toh(track->len);
    track->last_event_status_avail = 0;

    ctx->status = DECODE_EVENT_DELTA;

    return MIDI_OK;
}

int midi_decode_event_delta(midi_context_t *ctx, uint8_t *buf, int *len)
{
    midi_track_t *track = &ctx->track;
    midi_event_t *event = &track->event;

    if (midi_number(buf, len, &ctx->tmp.value) != MIDI_OK) {
        return MIDI_AGAIN;
    }

    event->delta = ctx->tmp.value;
    event->is_meta = 0;
    ctx->status = DECODE_EVENT_STATUS;

    return MIDI_OK;
}

int midi_decode_event_status(midi_context_t *ctx, uint8_t *buf, int *len)
{
    midi_track_t *track = &ctx->track;
    midi_event_t *event = &track->event;

    if (buf[0] < 0x80) {
        if (!track->last_event_status_avail) {
            LOG_ERROR("event status not found:0x%x", buf[0]);
            return MIDI_ABORT;
        }
        event->status = track->last_event_status;
        *len = 0;
    } else {
        event->status = buf[0];
        *len = 1;
    }

    track->last_event_status = event->status;
    track->last_event_status_avail = 1;

    if (event->status >= _FIRST_CHANNEL_EVENT && event->status <= _LAST_CHANNEL_EVENT) {
        ctx->status = DECODE_EVENT_PARAM1;
    } else if (event->status == _META_PREFIX || event->status == SYSEX || event->status == ESCAPE) {
        ctx->status = DECODE_EVENT_NON_CHANNEL;
    } else {
        LOG_ERROR("unsupport event status:0x%x", event->status);
        return MIDI_ABORT;
    }

    return MIDI_OK;
}

int midi_decode_event_param1(midi_context_t *ctx, uint8_t *buf, int *len)
{
    midi_track_t *track = &ctx->track;
    midi_event_t *event = &track->event;

    event->param1 = buf[0];
    *len = 1;
    if (event->status >= _FIRST_1BYTE_EVENT && event->status <= _LAST_1BYTE_EVENT) {
        event->param2 = 0;
        ctx->status = DECODE_EVENT_DELTA;
        midi_process_event(ctx, event);
    } else {
        ctx->status = DECODE_EVENT_PARAM2;
    }

    return MIDI_OK;
}

int midi_decode_event_param2(midi_context_t *ctx, uint8_t *buf, int *len)
{
    midi_track_t *track = &ctx->track;
    midi_event_t *event = &track->event;

    event->param2 = buf[0];
    *len = 1;
    ctx->status = DECODE_EVENT_DELTA;
    midi_process_event(ctx, event);

    return MIDI_OK;
}

int midi_decode_event_non_channel(midi_context_t *ctx, uint8_t *buf, int *len)
{
    int off = 0;
    midi_track_t *track = &ctx->track;
    midi_event_t *event = &track->event;

    if (event->status == _META_PREFIX) {
        event->is_meta = 1;
        event->status = buf[0];
        if (event->status < _FIRST_META_EVENT || event->status > _LAST_META_EVENT) {
            LOG_ERROR("invalid midi meta second event status:0x%x, not in range 0x00-0x7f", event->status);
            return MIDI_ABORT;
        }
        off += 1;
        *len -= off;
    }

    if (*len == 0) {
        *len = off;
        return MIDI_OK;
    }

    int ret = midi_number(buf + off, len, &ctx->tmp.total_len);
    *len += off;
    if (ret != MIDI_OK) {
        return MIDI_AGAIN;
    }

    if (event->is_meta && event->status == END_OF_TRACK) {
        // 0xFF 0x2F 0x00
        if (ctx->tmp.total_len != 0) {
            LOG_ERROR("invalid track end, expect 0 actual:0x%x", ctx->tmp.total_len);
            return MIDI_ABORT;
        }

        ctx->decode_tracks_count += 1;

        if (ctx->decode_tracks_count == ctx->header.num_tracks) {
            ctx->status = DECODE_COMPLETE;
        } else {
            ctx->status = DECODE_TRACK_HEADER;
        }

        return MIDI_OK;
    }

    ctx->status = DECODE_EVENT_DROP;

    return MIDI_AGAIN;
}

int midi_decode_event_drop(midi_context_t *ctx, uint8_t *buf, int *len)
{
    int off = 0;
    midi_track_t *track = &ctx->track;
    midi_event_t *event = &track->event;

    *len = MIN(ctx->tmp.total_len - ctx->tmp.drop_len, *len);
    ctx->tmp.drop_len += *len;
    if (ctx->tmp.drop_len < ctx->tmp.total_len) {
        return MIDI_AGAIN;
    }

    ctx->status = DECODE_EVENT_DELTA;

    return MIDI_OK;
}

int midi_decode_complete(midi_context_t *ctx, uint8_t *buf, int *len)
{
    return MIDI_OK;
}

typedef int (*midi_decode_func)(midi_context_t *ctx, uint8_t *buf, int *len);
midi_decode_func g_midi_decode_func[] = {
    midi_decode_header,
    midi_decode_track_header,
    midi_decode_event_delta,
    midi_decode_event_status,
    midi_decode_event_param1,
    midi_decode_event_param2,
    midi_decode_event_non_channel,
    midi_decode_event_drop,
    midi_decode_complete
};

int midi_decode(midi_context_t *ctx, uint8_t *buf, int len)
{
    int ret = MIDI_OK;
    int off = 0;
    int _len = 0;
    while (len > 0) {
        _len = len;
        ret = g_midi_decode_func[ctx->status](ctx, buf + off, &_len);
        if (ret == MIDI_ABORT) {
            return ret;
        }

        if (ret == MIDI_OK) {
            memset(&ctx->tmp, 0, sizeof(ctx->tmp));
        }

        ctx->decode_len += _len;

        off += _len;
        len -= _len;
    }

    return MIDI_OK;
}
