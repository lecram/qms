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

typedef enum EvType {PAC, VOL, PAN, VEL, PITCH} EvType;

typedef struct Event {
    uint32_t offset;
    uint32_t event;
} Event;

void qms_init();
void qms_setpac(int track, int pac);
void qms_setvol(int track, int midivol);
void qms_setpan(int track, int midipan);
void qms_setvelocity(int track, int voice, int velocity);
void qms_setnote(int track, int voice, int midipitch);
void qms_advance(unsigned int nsamples);
void qms_runevents(Event *evs, unsigned int nevs);
void qms_putsample(int16_t left, int16_t right);
