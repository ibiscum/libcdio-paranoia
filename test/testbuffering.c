/*
  Copyright (C) 2024 Rocky Bernstein <rocky@gnu.org>

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

/* test of routines in src/buffering_write.h/.c.
   Exercises small writes, writes spanning multiple internal buffer
   flushes (regression test for an overflow when a single write was
   larger than twice the internal buffer size), and switching between
   two file descriptors which forces an implicit flush.
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/buffering_write.h"

/* Must match (or exceed) the internal OUTBUFSZ in buffering_write.c so
   that we exercise multiple internal flush cycles in one call. */
#define BIG_SIZE (3 * 32 * 1024 + 173)

static int
write_and_verify(const char *path, const char *data, long size,
                  int chunked)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    perror("open (write)");
    return 1;
  }

  if (chunked) {
    /* Feed the data in small, uneven chunks to exercise buffering
       across multiple buffering_write() calls. */
    long pos = 0;
    long step = 731; /* arbitrary, not a divisor of BIG_SIZE */
    while (pos < size) {
      long n = size - pos;
      if (n > step) n = step;
      if (buffering_write(fd, (char *) data + pos, n)) {
        fprintf(stderr, "buffering_write failed (chunked)\n");
        buffering_close(fd);
        return 1;
      }
      pos += n;
    }
  } else {
    if (buffering_write(fd, (char *) data, size)) {
      fprintf(stderr, "buffering_write failed\n");
      buffering_close(fd);
      return 1;
    }
  }
  buffering_close(fd);

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    perror("open (read back)");
    return 1;
  }
  char *readback = malloc(size);
  long total = 0;
  while (total < size) {
    ssize_t n = read(fd, readback + total, size - total);
    if (n <= 0) break;
    total += n;
  }
  close(fd);
  unlink(path);

  int i_rc = 0;
  if (total != size) {
    fprintf(stderr, "expected %ld bytes read back, got %ld\n", size, total);
    i_rc = 1;
  } else if (memcmp(data, readback, size) != 0) {
    fprintf(stderr, "readback data does not match original (%s)\n",
            chunked ? "chunked" : "single write");
    i_rc = 1;
  }
  free(readback);
  return i_rc;
}

int
main(int argc, const char *argv[])
{
  int i_rc = 0;
  long i;
  char *data = malloc(BIG_SIZE);
  for (i = 0; i < BIG_SIZE; i++)
    data[i] = (char) (i & 0xff);

  /* Small, single write smaller than the internal buffer. */
  i_rc |= write_and_verify("testbuffering-small.dat", data, 100, 0);

  /* Single write spanning several internal buffer flushes -
     regression test for a buffer overflow when num was larger than
     twice OUTBUFSZ. */
  i_rc |= write_and_verify("testbuffering-big.dat", data, BIG_SIZE, 0);

  /* Same data, fed through many small buffering_write() calls. */
  i_rc |= write_and_verify("testbuffering-chunked.dat", data, BIG_SIZE, 1);

  /* Switching between two file descriptors forces an implicit flush
     of the previous descriptor's buffered data. */
  {
    int fd1 = open("testbuffering-a.dat", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int fd2 = open("testbuffering-b.dat", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd1 < 0 || fd2 < 0) {
      perror("open (switch test)");
      i_rc = 1;
    } else {
      char part_a1[] = "AAAA";
      char part_b[] = "BBBB";
      char part_a2[] = "CCCC";
      buffering_write(fd1, part_a1, 4);
      buffering_write(fd2, part_b, 4);
      buffering_write(fd1, part_a2, 4);
      buffering_close(fd1);
      buffering_close(fd2);

      char buf[8];
      int fd, n;
      fd = open("testbuffering-a.dat", O_RDONLY);
      n = read(fd, buf, sizeof(buf));
      close(fd);
      if (n != 8 || memcmp(buf, "AAAACCCC", 8) != 0) {
        fprintf(stderr, "fd switch test: file a mismatch\n");
        i_rc = 1;
      }
      fd = open("testbuffering-b.dat", O_RDONLY);
      n = read(fd, buf, sizeof(buf));
      close(fd);
      if (n != 4 || memcmp(buf, "BBBB", 4) != 0) {
        fprintf(stderr, "fd switch test: file b mismatch\n");
        i_rc = 1;
      }
    }
    unlink("testbuffering-a.dat");
    unlink("testbuffering-b.dat");
  }

  free(data);
  if (argc > 1 && i_rc == 0)
    printf("all buffering_write tests passed\n");
  exit(i_rc);
}
