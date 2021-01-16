#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "qms.h"
#include "smf.h"

typedef struct TempoChange {
    uint32_t offset;
    uint32_t usecs_per_quarter;
} TempoChange;

#define MAX_TEMPO_CHANGES   1024

static TempoChange tempo_changes[MAX_TEMPO_CHANGES];

static uint8_t
read_u8(int fd)
{
    uint8_t byte;
    read(fd, &byte, 1);
    return byte;
}

static uint16_t
read_beu16(int fd)
{
    return (((uint16_t) read_u8(fd)) << 8) + read_u8(fd);
}

static uint32_t
read_beu32(int fd)
{
    return (((uint32_t) read_beu16(fd)) << 16) + read_beu16(fd);
}

static uint32_t
read_vlv(int fd)
{
    uint8_t byte;
    uint32_t v = 0;
    do {
        read(fd, &byte, 1);
        v <<= 7;
        v += byte & 0x7F;
    } while (byte & 0x80);
    return v;
}

int
cmp_offset(const void *a, const void *b)
{
    return *(uint32_t *) a - *(uint32_t *) b;
}

void
ticks2samples(uint16_t ticks_per_quarter, int ntcs, Event *evs, int nevs)
{
    uint32_t usecs_per_quarter, offset, total_samples;
    uint32_t delta, usecs, samples;
    int tci, evi;
    tci = 0;
    usecs_per_quarter = 500000; /* MIDI default */
    offset = 0;
    total_samples = 0;
    for (evi = 0; evi < nevs; evi++) {
        while (tci < ntcs && tempo_changes[tci].offset <= offset)
            usecs_per_quarter = tempo_changes[tci++].usecs_per_quarter;
        delta = evs[evi].offset - offset;
        usecs = delta * usecs_per_quarter / ticks_per_quarter;
        samples = usecs * (R/100) / 10000;
        total_samples += samples;
        evs[evi].offset = total_samples;
        offset += delta;
    }
}

#define add_ev(ev)  (evs[nevs++] = (Event) {offset, (ev)})
#define add_tc(upq) (tempo_changes[ntcs++] = (TempoChange) {offset, (upq)})

SMFError
qms_smf2evs(const char *fname, Event *evs, int maxnevs, int *pnevs)
{
    uint32_t data_length;
    uint16_t format, ntracks, division;
    uint16_t ticks_per_quarter;
    uint32_t usecs_per_quarter;
    uint32_t offset;
    uint8_t byte, status, chan, arg, vel;
    uint16_t param, wheel;
    uint8_t semirange, centrange;
    int smf_track;
    int ntcs, nevs;
    ntcs = nevs = 0;
    int fd = open(fname, O_RDONLY);
    if (fd == -1) return SMF_NOFILE;
    if (read_beu32(fd) != 0x4D546864) return SMF_BADSIG;
    data_length = read_beu32(fd); /* always 6 */
    format = read_beu16(fd);
    if (format > 1) return SMF_BADFMT;
    ntracks = read_beu16(fd);
    division = read_beu16(fd);
    if (division & 0x8000) return SMF_BADDIV;
    ticks_per_quarter = division;
    status = 0; /* not meaningful, just to initialize variable */
    param = 0;
    semirange = 2;
    centrange = 0;
    for (smf_track = 0; smf_track < ntracks; smf_track++) {
        if (read_beu32(fd) != 0x4D54726B) return SMF_BADSIG;
        data_length = read_beu32(fd);
        for (offset = 0;;) {
            offset += read_vlv(fd);
            byte = read_u8(fd);
            if (byte == 0xFF) {
                /* Meta Event */
                arg = read_u8(fd);
                if (arg == 0x2F) {
                    /* End of Track */
                    (void) read_u8(fd);
                    break;
                } else if (arg == 0x51) {
                    /* Tempo Change */
                    /* safely ignore a byte 0x03 meaning length */
                    usecs_per_quarter = read_beu32(fd) & 0xFFFFFF;
                    if (ntcs == MAX_TEMPO_CHANGES) return SMF_TOOBIG;
                    add_tc(usecs_per_quarter);
                } else {
                    /* ignore any other kind of meta events */
                    data_length = read_vlv(fd);
                    lseek(fd, data_length, SEEK_CUR);
                }
            } else if (byte >= 0xF0) {
                /* SysEx Event (ignore) */
                data_length = read_vlv(fd);
                lseek(fd, data_length, SEEK_CUR);
            } else {
                /* MIDI Event */
                if (byte < 0x80) {
                    /* running status */
                    arg = byte;
                } else {
                    status = byte;
                    arg = read_u8(fd);
                }
                chan = status & 0xF;
                switch (status >> 4) {
                case 0x8:   /* note off */
                    vel = read_u8(fd);
                    if (chan == 0x9) break;
                    add_ev(qms_ev_vel(chan, 0, 0));
                    break;
                case 0x9:   /* note on */
                    vel = read_u8(fd);
                    if (chan == 0x9) break;
                    add_ev(qms_ev_vel(chan, 0, vel));
                    if (vel > 0) {
                        add_ev(qms_ev_pitch(chan, 0, arg));
                    }
                    break;
                case 0xB:   /* control change */
                    switch (arg) {
                    case 0x06:  /* data entry MSB */
                        if (param == 0x0000)    /* pitch bend range */
                            semirange = read_u8(fd);
                        else (void) read_u8(fd);
                        break;
                    case 0x07:  /* channel volume */
                        add_ev(qms_ev_vol(chan, read_u8(fd)));
                        break;
                    case 0x0A:  /* channel pan */
                        add_ev(qms_ev_pan(chan, read_u8(fd)));
                        break;
                    case 0x26:  /* data entry LSB */
                        if (param == 0x0000)    /* pitch bend range */
                            centrange = read_u8(fd);
                        else (void) read_u8(fd);
                        break;
                    case 0x64:  /* RPN LSB */
                        param = (param & 0xFF00) | read_u8(fd);
                        break;
                    case 0x65:  /* RPN MSB */
                        param = (param & 0x00FF) | (read_u8(fd) << 7);
                        break;
                    default:    /* other control change (ignore) */
                        (void) read_u8(fd);
                    }
                    break;
                case 0xC:   /* program change */
                    add_ev(qms_ev_pac(chan, arg % NPACS));
                    break;
                case 0xE:   /* pitch wheel change */
                    wheel = arg | (read_u8(fd) << 7);
                    wheel = ((((int16_t) wheel >> 1) - 0x1000) * semirange) + 0x2000;
                    add_ev(qms_ev_wheel(chan, 0, wheel));
                }
            }
            if (nevs == maxnevs - 1) return SMF_TOOBIG;
        }
    }
    close(fd);
    qsort(evs, nevs, sizeof(Event), cmp_offset);
    qsort(tempo_changes, ntcs, sizeof(TempoChange), cmp_offset);
    ticks2samples(ticks_per_quarter, ntcs, evs, nevs);
    if (pnevs) *pnevs = nevs;
    return SMF_OK;
}
