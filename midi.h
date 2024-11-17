#include <stdint.h>

#define MIDI_HEADER_MAGIC       0x6468544d
#define MIDI_TRACK_HEADER_MAGIC 0x6b72544d

#define MIDI_OK     0
#define MIDI_AGAIN  -1
#define MIDI_ABORT  -0xFF

#define BUF_SIZE                32
#define MIDI_HEADER_LEN         14U
#define MIDI_TRACK_HEADER_LEN   8U

#define LOG_ERROR(fmt, ...) do {fprintf(stderr, "%s:%u -- "fmt"\n", __FILE__, __LINE__, ##__VA_ARGS__);} while (0)
#define LOG_INFO(fmt, ...) do {fprintf(stdout, "%s:%u -- "fmt"\n", __FILE__, __LINE__, ##__VA_ARGS__);} while (0)
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
    DECODE_EVENT_DELTA,
    DECODE_EVENT_STATUS,
    DECODE_EVENT_PARAM1,
    DECODE_EVENT_PARAM2,
    DECODE_EVENT_NON_CHANNEL,
    DECODE_EVENT_DROP,
    DECODE_EVENT_SET_TEMPO,
    DECODE_COMPLETE
} decode_status_t;

typedef struct {
    uint32_t magic; // MThd
    uint32_t len; // always is 6
    uint16_t format; // 0: single Mtrk chunks sync; 1: two or more MTrk chunks sync; 2: multi MTrk chunks async
    uint16_t num_tracks; // number of chunks
    uint16_t ticks_per_quarter; // pulses per beat
} midi_header_t;

typedef struct {
    uint32_t delta;
    uint8_t status;
    uint8_t param1;
    uint8_t param2;
    uint8_t is_meta;
} midi_event_t;

typedef struct {
    uint32_t magic;
    uint32_t len;
    uint8_t last_event_status_avail;
    uint8_t last_event_status;
    midi_event_t event;
} midi_track_t;

struct midi_context;
typedef void (*on_event_func)(struct midi_context *ctx, midi_event_t *event);
typedef struct midi_context {
    midi_header_t header;
    midi_track_t track;

    uint32_t tempo;
    uint32_t decode_tracks_count;
    uint32_t decode_len;

    decode_status_t status;

    on_event_func on_event;
    void *user_data;

    union {
        struct {
            uint8_t buf_off;
            char buf[BUF_SIZE];
        };
        struct {
            uint32_t total_len;
            uint32_t drop_len;
        };
        uint32_t value;
    } tmp;
} midi_context_t;

int midi_decode(midi_context_t *ctx, uint8_t *buf, uint16_t len);
double midi_note_to_freq(uint8_t note);