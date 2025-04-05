CTARGETS=unit_test test_throughput test_throughput_nonAdaptive test_throughput_adaptive
CXXTARGETS=

ifndef D
	DEBUG=
	OPT=-O3 -DNDEBUG
	SPLINTERPATH=external/splinterdb/build/release/lib
	#SPLINTERPATH=external/splinterdb/btree
else
	DEBUG=-g
	OPT=-O0
	SPLINTERPATH=external/splinterdb/build/debug/lib
	#SPLINTERPATH=external/splinterdb/btree
endif

ifdef NH
	ARCH=
else
	ARCH=-msse4.2 -D__SSE4_2_
endif

ifdef P
	PROFILE=-pg -no-pie # for bug in gprof.
endif

LOC_INCLUDE=include
LOC_SRC=src
LOC_TEST=test
OBJDIR=obj

CC = gcc -std=gnu11
CXX = g++ -std=c++11
LD= gcc -std=gnu11

CXXFLAGS = -Wall $(DEBUG) $(PROFILE) $(OPT) $(ARCH) -m64 -I. -Iinclude -Iexternal/splinterdb/include -DSPLINTERDB_PLATFORM_DIR=platform_linux -DSKIP_BOOL_DEF -D_GNU_SOURCE

LDFLAGS = $(DEBUG) $(PROFILE) $(OPT) -lpthread -lssl -lcrypto -lm -L$(SPLINTERPATH) -lsplinterdb -Wl,-rpath=$(SPLINTERPATH)
#LDFLAGS += -L/usr/lib/ -lstxxl

#
# declaration of dependencies
#

all: $(CTARGETS) #$(CXXTARGETS)

# dependencies between programs and .o files

test_throughput:						$(OBJDIR)/test_throughput.o $(OBJDIR)/gqf.o $(OBJDIR)/gqf_file.o \
										$(OBJDIR)/hashutil.o $(OBJDIR)/splinter_util.o $(OBJDIR)/test_driver.o \
										$(OBJDIR)/partitioned_counter.o $(OBJDIR)/ll_table.o $(OBJDIR)/rand_util.o

unit_test:						$(OBJDIR)/unit_test.o $(OBJDIR)/gqf.o $(OBJDIR)/gqf_file.o \
										$(OBJDIR)/hashutil.o $(OBJDIR)/splinter_util.o \
										$(OBJDIR)/partitioned_counter.o $(OBJDIR)/ll_table.o $(OBJDIR)/rand_util.o

test_throughput_nonAdaptive:			$(OBJDIR)/test_throughput_nonAdaptive.o $(OBJDIR)/gqf.o $(OBJDIR)/gqf_file.o \
										$(OBJDIR)/hashutil.o $(OBJDIR)/splinter_util.o $(OBJDIR)/test_driver.o \
										$(OBJDIR)/partitioned_counter.o $(OBJDIR)/ll_table.o $(OBJDIR)/rand_util.o

test_throughput_adaptive:				$(OBJDIR)/test_throughput_adaptive.o $(OBJDIR)/gqf.o $(OBJDIR)/gqf_file.o \
										$(OBJDIR)/hashutil.o $(OBJDIR)/splinter_util.o $(OBJDIR)/test_driver.o \
										$(OBJDIR)/partitioned_counter.o $(OBJDIR)/ll_table.o $(OBJDIR)/rand_util.o


# dependencies between .o files and .cc (or .c) files

$(OBJDIR)/gqf.o:						$(LOC_SRC)/gqf.c $(LOC_INCLUDE)/gqf.h
$(OBJDIR)/gqf_file.o:					$(LOC_SRC)/gqf_file.c $(LOC_INCLUDE)/gqf_file.h
$(OBJDIR)/hashutil.o:					$(LOC_SRC)/hashutil.c $(LOC_INCLUDE)/hashutil.h
$(OBJDIR)/partitioned_counter.o:		$(LOC_INCLUDE)/partitioned_counter.h
$(OBJDIR)/ll_table.o:					$(LOC_SRC)/ll_table.c $(LOC_INCLUDE)/ll_table.h
$(OBJDIR)/splinter_util.o:				$(LOC_SRC)/splinter_util.c $(LOC_INCLUDE)/splinter_util.h# $(OBJDIR)/gqf.o $(OBJDIR)/gqf_file.o
$(OBJDIR)/test_driver.o:				$(LOC_SRC)/test_driver.c $(LOC_INCLUDE)/test_driver.h# $(OBJDIR)/gqf.o $(OBJDIR)/gqf_file.o $(OBJDIR)/splinter_util.o

#
# generic build rules
#

$(CTARGETS):
	$(LD) $^ -o $@ $(LDFLAGS)

$(SPLTARGETS):
	$(LD) $^ -o $@ $(LDFLAGS)

$(CXXTARGETS):
	$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(LOC_SRC)/%.cc | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDE) $< -c -o $@

$(OBJDIR)/%.o: $(LOC_SRC)/%.c | $(OBJDIR)
	$(CC) $(CXXFLAGS) $(INCLUDE) $< -c -o $@

$(OBJDIR)/%.o: $(LOC_TEST)/%.c | $(OBJDIR)
	$(CC) $(CXXFLAGS) $(INCLUDE) $< -c -o $@

$(OBJDIR)/%.o: $(LOC_TEST)/%.cc | $(OBJDIR)
	$(CC) $(CXXFLAGS) $(INCLUDE) $< -c -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(CTARGETS) $(CXXTARGETS) $(SPLTARGETS) core
