/*
  Copyright (C) 2026 Rocky Bernstein <rocky@gnu.org>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Regression test for src/header.h/.c: verifies that the WAV/AIFF/AIFC
   header writers emit the expected magic tags, chunk layout and size
   fields for a given audio byte count. */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/header.h"

#define TESTFILE "testheader.dat"

typedef void (*header_writer_t)(int, long int);

static long
get_le(const unsigned char *p, int n)
{
  long v = 0;
  while (n--)
    v = (v << 8) | p[n];
  return v;
}

static long
get_be(const unsigned char *p, int n)
{
  long v = 0;
  int i;
  for (i = 0; i < n; i++)
    v = (v << 8) | p[i];
  return v;
}

static unsigned char *
write_and_read(header_writer_t fn, long bytes, long *out_size)
{
  unsigned char *buf;
  long size;
  int fd = open(TESTFILE, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    perror("open");
    exit(1);
  }
  fn(fd, bytes);
  size = lseek(fd, 0, SEEK_CUR);
  if (size < 0) {
    perror("lseek");
    exit(1);
  }
  buf = malloc(size);
  if (!buf || lseek(fd, 0, SEEK_SET) < 0 || read(fd, buf, size) != size) {
    perror("read back");
    exit(1);
  }
  close(fd);
  unlink(TESTFILE);
  *out_size = size;
  return buf;
}

int
main(void)
{
  int failures = 0;
  const long bytes = 2352 * 10; /* arbitrary audio data size */

  /* WAV: RIFF/WAVEfmt/data chunks, little-endian size fields. */
  {
    long size;
    unsigned char *buf = write_and_read(WriteWav, bytes, &size);
    if (size != 44) {
      fprintf(stderr, "WAV: header size %ld != 44\n", size);
      failures++;
    } else if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVEfmt ", 8) ||
              memcmp(buf + 36, "data", 4)) {
      fprintf(stderr, "WAV: chunk tag mismatch\n");
      failures++;
    } else if (get_le(buf + 4, 4) != bytes + 44 - 8) {
      fprintf(stderr, "WAV: RIFF size field wrong\n");
      failures++;
    } else if (get_le(buf + 40, 4) != bytes) {
      fprintf(stderr, "WAV: data size field wrong\n");
      failures++;
    } else {
      printf("WAV header okay\n");
    }
    free(buf);
  }

  /* AIFF: FORM/COMM/SSND chunks, big-endian size fields. */
  {
    long size;
    unsigned char *buf = write_and_read(WriteAiff, bytes, &size);
    if (size != 54) {
      fprintf(stderr, "AIFF: header size %ld != 54\n", size);
      failures++;
    } else if (memcmp(buf, "FORM", 4) || memcmp(buf + 8, "AIFF", 4) ||
              memcmp(buf + 12, "COMM", 4) || memcmp(buf + 38, "SSND", 4)) {
      fprintf(stderr, "AIFF: chunk tag mismatch\n");
      failures++;
    } else if (get_be(buf + 4, 4) != bytes + 54 - 8) {
      fprintf(stderr, "AIFF: FORM size field wrong\n");
      failures++;
    } else if (get_be(buf + 42, 4) != bytes + 8) {
      fprintf(stderr, "AIFF: SSND size field wrong\n");
      failures++;
    } else {
      printf("AIFF header okay\n");
    }
    free(buf);
  }

  /* AIFC: FORM/FVER/COMM/SSND chunks, big-endian size fields. */
  {
    long size;
    unsigned char *buf = write_and_read(WriteAifc, bytes, &size);
    if (size != 86) {
      fprintf(stderr, "AIFC: header size %ld != 86\n", size);
      failures++;
    } else if (memcmp(buf, "FORM", 4) || memcmp(buf + 8, "AIFC", 4) ||
              memcmp(buf + 12, "FVER", 4) || memcmp(buf + 24, "COMM", 4) ||
              memcmp(buf + 70, "SSND", 4)) {
      fprintf(stderr, "AIFC: chunk tag mismatch\n");
      failures++;
    } else if (get_be(buf + 4, 4) != bytes + 86 - 8) {
      fprintf(stderr, "AIFC: FORM size field wrong\n");
      failures++;
    } else if (get_be(buf + 74, 4) != bytes + 8) {
      fprintf(stderr, "AIFC: SSND size field wrong\n");
      failures++;
    } else {
      printf("AIFC header okay\n");
    }
    free(buf);
  }

  return failures ? 1 : 0;
}
