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

#include "midi.h"

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
    while (1) {;
        int ret = read(fd, buf, sizeof(buf));
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

    assert(ctx.status == DECODE_COMPLETE);
    close(fd);
    return 0;
}
