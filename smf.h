#ifndef SMF_H
#define SMF_H

typedef enum SMFError {
    SMF_OK, SMF_NOFILE, SMF_BADSIG, SMF_BADFMT, SMF_BADDIV, SMF_TOOBIG
} SMFError;

SMFError qms_smf2evs(const char *fname, Event *evs, int maxnevs, int *pnevs);

#endif /* SMF_H */
