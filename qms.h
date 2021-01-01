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

void qms_init();
void qms_setpac(int track, int pac);
void qms_setvolume(int track, int midivol);
void qms_setpan(int track, int midipan);
void qms_setnote(int track, int voice, int velocity, int midipitch);
void qms_advance(int nsamples);
void qms_putsample(int16_t left, int16_t right);
