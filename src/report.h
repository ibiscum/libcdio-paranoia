/*
  Copyright (C) 2008 Rocky Bernstein <rocky@gnu.org>
*/

extern int verbose;
extern int quiet;
extern FILE *reportfile;

#define report(...)                                                            \
  do {                                                                         \
    if (!quiet) {                                                              \
      fprintf(stderr, __VA_ARGS__);                                            \
      fputc('\n', stderr);                                                     \
    }                                                                          \
    if (reportfile) {                                                          \
      fprintf(reportfile, __VA_ARGS__);                                        \
      fputc('\n', reportfile);                                                 \
    }                                                                          \
  } while (0)
#define reportC(...)                                                           \
  do {                                                                         \
    if (!quiet) {                                                              \
      fprintf(stderr, __VA_ARGS__);                                            \
    }                                                                          \
    if (reportfile) {                                                          \
      fprintf(reportfile, __VA_ARGS__);                                        \
    }                                                                          \
  } while (0)
#define printC(...)                                                            \
  do {                                                                         \
    if (!quiet) {                                                              \
      fprintf(stderr, __VA_ARGS__);                                            \
    }                                                                          \
  } while (0)
#define logC(...)                                                              \
  do {                                                                         \
    if (reportfile) {                                                          \
      fprintf(reportfile, __VA_ARGS__);                                        \
    }                                                                          \
  } while (0)
