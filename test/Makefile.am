####################################################
# Things for regression testing
####################################################
#
#

SUBDIRS = cdda_interface data

testparanoia=testparanoia
testparanoia_LDADD = $(LIBCDIO_PARANOIA_LIBS) $(LIBCDIO_CDDA_LIBS) $(LIBCDIO_LIBS) $(LTLIBICONV)

hack = $(testparanoia)

DATA_DIR       = @abs_top_srcdir@/test/data

AM_CPPFLAGS = -I$(top_srcdir) $(LIBCDIO_CFLAGS) $(LIBCDIO_PARANOIA_CFLAGS)

check_SCRIPTS = check_paranoia.sh endian.sh check_start_track_not_one.sh
# If we beefed this up so it checked to see if a CD-DA was loaded
# it could be an automatic test. But for now, not so.
#               check_paranoia.sh

check_start_track_not_one.sh: get_libcdio_version

check_PROGRAMS = testparanoia testutils testbuffering testheader testreport get_libcdio_version

testbuffering_SOURCES = testbuffering.c ../src/buffering_write.c ../src/buffering_write.h

testheader_SOURCES = testheader.c ../src/header.c ../src/header.h

testreport_SOURCES = testreport.c ../src/report.c ../src/report.h

check_DATA = cd-paranoia-log.right

EXTRA_DIST = $(check_SCRIPTS) $(check_DATA)

TESTS = $(check_PROGRAMS) $(check_SCRIPTS)

MOSTLYCLEANFILES = core core.* *.dump cdda-orig.wav cdda-try.wav *.raw *.bin *.cue get_libcdio_version testbuffering-*.dat testheader.dat

test: check-am

# extra dependencies of tests
check-am: get_libcdio_version

CFLAGS += @LIBCDIO_CFLAGS@
