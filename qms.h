#ifndef QMS_H
#define QMS_H

#include <stdint.h>

#define NTRACKS     16
#define NVOICES     8

#define NPACS       2

/* N = 2^NEXP */
#define NEXP    11
/* wavetable size in samples */
#define N       (1 << NEXP)
/* sample rate in samples per second*/
#define R       44100

typedef enum EvType {END, PAC, VOL, PAN, VEL, PITCH, WHEEL} EvType;

typedef struct Event {
    uint32_t offset;
    uint32_t event;
} Event;

typedef struct Seeker {
    Event *evs;
    unsigned int nevs;
    unsigned int ev_i;
    unsigned int smp_i;
} Seeker;

#define qms_ev_pac(t, pac)      (((t) << 28) | (PAC << 16) | (pac))
#define qms_ev_vol(t, vol)      (((t) << 28) | (VOL << 16) | (vol))
#define qms_ev_pan(t, pan)      (((t) << 28) | (PAN << 16) | (pan))
#define qms_ev_vel(t, v, vel)   (((t) << 28) | ((v) << 24) | (VEL << 16) | (vel))
#define qms_ev_pitch(t, v, pit) (((t) << 28) | ((v) << 24) | (PITCH << 16) | (pit))
#define qms_ev_wheel(t, v, whl) (((t) << 28) | ((v) << 24) | (WHEEL << 16) | (whl))

void qms_init();
void qms_setpac(int track, int pac);
void qms_setvol(int track, int midivol);
void qms_setpan(int track, int midipan);
void qms_setvelocity(int track, int voice, int velocity);
void qms_setnote(int track, int voice, int midipitch);
void qms_setwheel(int track, int voice, int wheel);
void qms_advance(unsigned int nsamples);
int  qms_runevent(Event *ev);
void qms_runevents(Event *evs, unsigned int nevs);
void qms_load(Seeker *seeker, Event *evs, unsigned int nevs);
void qms_seek(Seeker *seeker, unsigned int nsamples);
int  qms_play(Seeker *seeker, unsigned int nsamples);
void qms_putsample(int16_t left, int16_t right);

#endif /* QMS_H */
