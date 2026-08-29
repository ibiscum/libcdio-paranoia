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

/* Regression test for src/report.h/.c: the report()/reportC()/printC()/
   logC() macros must expand to a single statement (so they are safe as
   the sole branch of an if/else), and must gate output on 'quiet' and
   'reportfile' as documented. */

#include <stdio.h>
#include <string.h>

#include "../src/report.h"

static int failures = 0;

static void
check(const char *what, const char *expect, const char *got)
{
  if (strcmp(expect, got) != 0) {
    fprintf(stderr, "%s: expected '%s', got '%s'\n", what, expect, got);
    failures++;
  } else {
    printf("%s okay\n", what);
  }
}

int
main(void)
{
  /* Dangling-else regression guard: this must compile. If any of these
     macros were ever redefined without a do/while(0) wrapper, an
     unbraced if/else using them as the sole branch would fail to
     compile. */
  if (1)
    report("compile check");
  else
    report("unreachable");

  if (1)
    reportC("compile check");
  else
    reportC("unreachable");

  if (1)
    printC("compile check");
  else
    printC("unreachable");

  if (1)
    logC("compile check");
  else
    logC("unreachable");

  /* logC() writes only to reportfile, never to stderr. */
  {
    char buf[64] = {0};
    reportfile = tmpfile();
    if (!reportfile) {
      perror("tmpfile");
      return 1;
    }
    logC("hello %d", 42);
    rewind(reportfile);
    fread(buf, 1, sizeof(buf) - 1, reportfile);
    check("logC", "hello 42", buf);
    fclose(reportfile);
    reportfile = NULL;
  }

  /* quiet suppresses stderr but report() still writes to reportfile. */
  {
    char buf[64] = {0};
    quiet = 1;
    reportfile = tmpfile();
    if (!reportfile) {
      perror("tmpfile");
      return 1;
    }
    report("quiet %d", 7);
    rewind(reportfile);
    fread(buf, 1, sizeof(buf) - 1, reportfile);
    check("report (quiet mode)", "quiet 7\n", buf);
    fclose(reportfile);
    reportfile = NULL;
    quiet = 0;
  }

  return failures ? 1 : 0;
}
