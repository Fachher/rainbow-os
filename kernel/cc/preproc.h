#ifndef CC_PREPROC_H
#define CC_PREPROC_H

/* Preprocess source: expand #define, #include, #ifdef/#ifndef/#else/#endif.
   Returns length of output, or -1 on error. */
int preprocess(const char *source, int len, char *out, int out_max);

#endif
