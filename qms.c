#include "qms.h"

typedef struct TrackState {
    int pac;   /* < NPACS */
    int vol;   /* 127-0         default (zero) means maximum */
    int pan;   /* (-64)-(+63)   default (zero) means center */
} TrackState;

typedef struct VoiceState {
    unsigned int phase_acc;   /* fixed point with NEXP.16 precision */
    unsigned int phase_step;  /* fixed point with NEXP.16 precision */
    int velocity;    /* 0x00-0x7F */
    int pitch;       /* 0x00-0x7F */
    int wheel;       /* S7.8    default (zero) means center */
    /* int note_age; */
} VoiceState;

static int16_t wavetables[NPACS][N];
static TrackState tracks[NTRACKS];
static VoiceState voices[NTRACKS][NVOICES];

/* sine approximation using Bhaskara I's formula
 * input is in [0;2048]
 * output is in [-INT16_MAX;INT16_MAX] */
static int16_t
sinb1(unsigned int i)
{
    uint32_t c, n, d;
    int16_t s = 1 - (i >> 10 << 1);
    i &= 1023;
    c = i*(46080 - 45*i);
    n = 360*c;
    d = 162005 - (90*c >> 15);
    return s * (int16_t) (n/d);
}

/* approximation of exp(x)-1 using power series
 * both input and output are U0.32 */
uint32_t
fxp_expm1(uint32_t x)
{
    uint32_t e, t;
    int i;
    e = 0;
    t = x;
    for (i = 2; t; i++) {
        e += t;
        t = (t>>13) * (x>>19) / i;
    }
    return e;
}

void
qms_init()
{
    unsigned int i;
    int32_t tmp;
    for (i = 0; i < N; i++) {
        /* sawtooth */
        tmp = (i * INT16_MAX >> (NEXP-1)) - INT16_MAX;
        wavetables[0][i] = (int16_t) tmp;
    }
    for (i = 0; i < N; i++) {
        /* sine */
        wavetables[1][i] = sinb1(i);
    }
}

void
qms_setpac(int track, int pac)
{
    tracks[track].pac = pac;
}

void
qms_setvol(int track, int midivol)
{
    tracks[track].vol = 127 - midivol;
}

void
qms_setpan(int track, int midipan)
{
    tracks[track].pan = midipan - 64;
}

static unsigned int
midipitch2step(int m, int w)
{
    unsigned int o, n; /* m = o * 12 + n */
    int neg_wheel;
    uint32_t nwc;
    uint32_t c = 0x0EC98200; /* ln(2^(1/12)) in U0.32 */
    neg_wheel = w & 0x8000;
    if (neg_wheel) {
        w = 0x10000 - w;
        m -= w >> 8;
    } else {
        m += w >> 8;
    }
    w &= 0xFF;
    for (n = m+3, o = 0; n >= 12; n -= 12, o++) ;
    /* the following takes advantage of assumed constants:
     *   R = 44100 = 440 * 2205 / 22
     *   N = 2048 = 2^11 */
    if (neg_wheel)
        nwc = ((n<<8)-w)*(c>>8);
    else
        nwc = ((n<<8)+w)*(c>>8);
    return ((fxp_expm1(nwc)>>5) * 22 / (2205<<6) + (22<<21) / 2205) << o;
}

void
qms_setvelocity(int track, int voice, int velocity)
{
    voices[track][voice].velocity = velocity;
}

void
qms_setnote(int track, int voice, int midipitch)
{
    int wheel = voices[track][voice].wheel;
    voices[track][voice].phase_step = midipitch2step(midipitch, wheel);
    voices[track][voice].pitch = midipitch;
}

void
qms_setwheel(int track, int voice, int wheel)
{
    int midipitch = voices[track][voice].pitch;
    voices[track][voice].phase_step = midipitch2step(midipitch, wheel);
    voices[track][voice].wheel = wheel;
}

void
qms_advance(unsigned int nsamples)
{
    int32_t left, right;
    int32_t amp;
    int16_t *pac;
    int lvol, rvol; /* 0 - 2^14 */
    TrackState *track;
    VoiceState *voice;
    int ti, vi;
    for (; nsamples; nsamples--) {
        left = right = 0;
        for (ti = 0; ti < NTRACKS; ti++) {
            track = &tracks[ti];
            lvol = (63 - track->pan) * (127 - track->vol);
            rvol = (63 + track->pan) * (127 - track->vol);
            pac = wavetables[track->pac];
            for (vi = 0; vi < NVOICES; vi++) {
                voice = &voices[ti][vi];
                amp = pac[voice->phase_acc >> 16] * voice->velocity >> 7;
                left  += amp * lvol >> 14;
                right += amp * rvol >> 14;
                voice->phase_acc += voice->phase_step;
                voice->phase_acc &= (1 << (NEXP + 16)) - 1;
            }
        }
        /* saturate before casting down to 16-bit to avoid bad behavior on overflow */
        left  =  left < INT16_MIN ? INT16_MIN : ( left > INT16_MAX ? INT16_MAX :  left);
        right = right < INT16_MIN ? INT16_MIN : (right > INT16_MAX ? INT16_MAX : right);
        qms_putsample((int16_t) left, (int16_t) right);
    }
}

int
qms_runevent(Event *ev)
{
    EvType ev_type;
    unsigned int track, voice, arg;
    int end = 0;
    track = ev->event >> 28;
    voice = ev->event >> 24 & 7;
    ev_type = ev->event >> 16 & 0xFF;
    arg = ev->event & 0xFFFF;
    switch (ev_type) {
    case END:
        end = 1;
        break;
    case PAC:
        qms_setpac(track, arg);
        break;
    case VOL:
        qms_setvol(track, arg);
        break;
    case PAN:
        qms_setpan(track, arg);
        break;
    case VEL:
        qms_setvelocity(track, voice, arg);
        break;
    case PITCH:
        qms_setnote(track, voice, arg);
        break;
    case WHEEL:
        qms_setwheel(track, voice, arg);
    }
    return end;
}

void
qms_runevents(Event *evs, unsigned int nevs)
{
    uint32_t total_samples = 0;
    for (; nevs--; evs++) {
        qms_advance(evs->offset - total_samples);
        total_samples = evs->offset;
        if (qms_runevent(evs))
            break;
    }
}
