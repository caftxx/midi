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
#include <unistd.h>

#include "midi.h"

void on_event(midi_context_t *ctx, midi_event_t *event)
{
    uint8_t type = event->status & 0xf0;
    if (type != NOTE_ON && type != NOTE_OFF) {
        return;
    }

    uint8_t channel = event->status & 0x0f;
    if (channel != 1) {
        return;
    }
    
    uint8_t duty = event->param2;
    uint16_t freq = midi_note_to_freq(event->param1);
    uint16_t delta_ms = event->delta / 1000;

    int wfd = (int)ctx->user_data;

    uint8_t buf[8] = {0};
    if (type == NOTE_ON) buf[0] = 0x80;
    buf[0] |= duty & 0x7f;
    buf[1] = freq & 0xff;
    buf[2] = (freq >> 8) & 0xf;
    buf[2] |= (delta_ms) & 0xf;
    buf[3] = (delta_ms >> 4) & 0xff;

    uint8_t ascii[16];
    snprintf(ascii, sizeof(ascii), "0x%x,", *((uint32_t*)buf));
    if (write(wfd, ascii, strlen(ascii)) != strlen(ascii)) {
        LOG_ERROR("fail to write new event:0x%x 0x%x 0x%x 0x%x 0x%x", buf[0], buf[1], buf[2], buf[3]);
        abort();
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        LOG_ERROR("missing midi file");
        return -1;
    }

    const char *fpath = argv[1];
    int rfd = open(fpath, O_RDONLY);
    if (rfd < 0) {
        LOG_ERROR("fail to open fpath:%s", fpath);
        return -1;
    }

    char new_fpath[64];
    snprintf(new_fpath, sizeof(new_fpath), "%s.h", fpath);
    int wfd = open(new_fpath, O_WRONLY | O_CREAT, 0666);

    char meta[32];
    snprintf(meta, sizeof(meta), "uint32_t midi_data[] = {");
    int ret = write(wfd, meta, strlen(meta));
    if (ret <= 0) {
        LOG_ERROR("fail to write header to new file:%s", new_fpath);
        return -1;
    }

    midi_context_t ctx = {0};
    ctx.on_event = on_event;
    ctx.user_data = (void *)wfd;

    uint8_t buf[BUF_SIZE] = {0};
    while (1) {;
        ret = read(rfd, buf, sizeof(buf));
        if (ret == 0) {
            break;
        } else if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            LOG_ERROR("some error happen, errno:%d", errno);
            break;
        }

        if (midi_decode(&ctx, buf, ret) != MIDI_OK) {
            break;
        }
    }

    ret = write(wfd, "};", 2);
    if (ret <= 0) {
        LOG_ERROR("fail to write header to new file:%s", new_fpath);
        return -1;
    }

    assert(ctx.status == DECODE_COMPLETE);
    close(rfd);
    close(wfd);
    return 0;
}
