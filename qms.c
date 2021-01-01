#include "qms.h"

typedef struct TrackState {
    int pac;   /* < NPACS */
    int vol;   /* 0-127 */
    int pan;   /* (-64)-(+63) */
} TrackState;

typedef struct VoiceState {
    unsigned int phase_acc;   /* fixed point with NEXP.NEXP precision */
    unsigned int phase_step;  /* fixed point with NEXP.NEXP precision */
    int velocity;    /* 0-127 */
    /* int note_age; */
} VoiceState;

static int16_t wavetables[NPACS][N];
static TrackState tracks[NTRACKS];
static VoiceState voices[NTRACKS][NVOICES];

/* integer frequencies of MIDI notes 0-11 multiplied by N */
/* to be used as fixed point with NEXP.NEXP precision */
/* python: [int(440 * 2**((m-69)/12) * N +0.5) for m in range(12)] */
static int freq[12] =
  {16744, 17740, 18795, 19912, 21096, 22351,
   23680, 25088, 26580, 28160, 29834, 31609};

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
qms_setvolume(int track, int midivol)
{
    tracks[track].vol = midivol;
}

void
qms_setpan(int track, int midipan)
{
    tracks[track].pan = midipan - 64;
}

static int
midipitch2step(int m)
{
    int o, n; /* m = o * 12 + n */
    for (n = m, o = 0; n >= 12; n -= 12, o++) ;
    return (freq[n] << (o+NEXP)) / R;
}

void
qms_setnote(int track, int voice, int velocity, int midipitch)
{
    voices[track][voice].velocity = velocity;
    voices[track][voice].phase_step = midipitch2step(midipitch);
}

void
qms_advance(int nsamples)
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
            lvol = (63 - track->pan) * track->vol;
            rvol = (63 + track->pan) * track->vol;
            pac = wavetables[track->pac];
            for (vi = 0; vi < NVOICES; vi++) {
                voice = &voices[ti][vi];
                amp = pac[voice->phase_acc >> NEXP] * voice->velocity >> 7;
                left  += amp * lvol >> 14;
                right += amp * rvol >> 14;
                voice->phase_acc += voice->phase_step;
                voice->phase_acc &= (1 << (NEXP << 1)) - 1;
            }
        }
        /* saturate before casting down to 16-bit to avoid bad behavior on overflow */
        left  =  left < INT16_MIN ? INT16_MIN : ( left > INT16_MAX ? INT16_MAX :  left);
        right = right < INT16_MIN ? INT16_MIN : (right > INT16_MAX ? INT16_MAX : right);
        qms_putsample((int16_t) left, (int16_t) right);
    }
}
