CXXTARGETS=bench_variants workload_gen

ifndef D
	DEBUG=
	OPT=-O3 -DNDEBUG
	SPLINTERPATH=$(CURDIR)/external/splinterdb/build/release/lib
	WTPATH=$(CURDIR)/external/wiredtiger/build
	#SPLINTERPATH=$(CURDIR)/external/splinterdb/btree
else
	DEBUG=-g
	OPT=-O0
	SPLINTERPATH=$(CURDIR)/external/splinterdb/build/release/lib
	WTPATH=$(CURDIR)/external/wiredtiger/build
	#SPLINTERPATH=$(CURDIR)/external/splinterdb/btree
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
LOC_BENCH=bench/cpp
OBJ_DIR=sponge/build/obj
BUILD_DIR=sponge/build
CQFDIR=other_filters/cqf/obj
CC = gcc -std=gnu11
CXX = g++ -std=c++17
LD= gcc -std=gnu11

CXXFLAGS = -Wall $(DEBUG) $(PROFILE) $(OPT) $(ARCH) -m64 -I. -Iinclude -Iexternal/splinterdb/include -Iexternal/cxxopts/include -Iexternal/wiredtiger/build/include -Iexternal -Ivariants -DSPLINTERDB_PLATFORM_DIR=platform_linux -DSKIP_BOOL_DEF -D_GNU_SOURCE

LDFLAGS = $(DEBUG) $(PROFILE) $(OPT) -lpthread -lssl -lcrypto -lm -L$(SPLINTERPATH) -L$(WTPATH) -lsplinterdb -lwiredtiger -Wl,-rpath=$(SPLINTERPATH):$(WTPATH)
#LDFLAGS += -L/usr/lib/ -lstxxl

ifdef USE_CQF
	CXXFLAGS += -DUSE_CQF
else
endif


ifdef SEVEN_BIT_OFFSET
		CXXFLAGS += -DSEVEN_BIT_OFFSET
endif

ifdef EXTRA_STATS
		CXXFLAGS += -DPERF
endif

#
# declaration of dependencies
#

all: $(CXXTARGETS)

# dependencies between programs and .o files

test_throughput:						$(OBJ_DIR)/test_throughput.o $(OBJ_DIR)/gqf.o $(OBJ_DIR)/gqf_file.o \
										$(OBJ_DIR)/hashutil.o $(OBJ_DIR)/splinter_util.o $(OBJ_DIR)/test_driver.o \
										$(OBJ_DIR)/partitioned_counter.o $(OBJ_DIR)/ll_table.o $(OBJ_DIR)/rand_util.o

unit_test:						$(OBJ_DIR)/unit_test.o $(OBJ_DIR)/gqf.o $(OBJ_DIR)/gqf_file.o \
										$(OBJ_DIR)/hashutil.o $(OBJ_DIR)/splinter_util.o \
										$(OBJ_DIR)/partitioned_counter.o $(OBJ_DIR)/ll_table.o $(OBJ_DIR)/rand_util.o

test_throughput_nonAdaptive:			$(OBJ_DIR)/test_throughput_nonAdaptive.o $(OBJ_DIR)/gqf.o $(OBJ_DIR)/gqf_file.o \
										$(OBJ_DIR)/hashutil.o $(OBJ_DIR)/splinter_util.o $(OBJ_DIR)/test_driver.o \
										$(OBJ_DIR)/partitioned_counter.o $(OBJ_DIR)/ll_table.o $(OBJ_DIR)/rand_util.o

ifdef USE_CQF
bench_variants:			$(OBJ_DIR)/bench_variants.o $(CQFDIR)/gqf.o $(CQFDIR)/gqf_file.o \
										$(OBJ_DIR)/hashutil.o $(OBJ_DIR)/splinter_util.o  \
										$(OBJ_DIR)/partitioned_counter.o $(OBJ_DIR)/ll_table.o $(OBJ_DIR)/rand_util.o
else
bench_variants:			$(OBJ_DIR)/bench_variants.o $(OBJ_DIR)/gqf.o $(OBJ_DIR)/gqf_file.o \
										$(OBJ_DIR)/hashutil.o $(OBJ_DIR)/splinter_util.o $(OBJ_DIR)/test_driver.o \
										$(OBJ_DIR)/partitioned_counter.o $(OBJ_DIR)/ll_table.o $(OBJ_DIR)/rand_util.o
endif

workload_gen:			$(OBJ_DIR)/workload_gen.o $(OBJ_DIR)/gqf.o $(OBJ_DIR)/gqf_file.o \
										$(OBJ_DIR)/hashutil.o $(OBJ_DIR)/splinter_util.o $(OBJ_DIR)/test_driver.o \
										$(OBJ_DIR)/partitioned_counter.o $(OBJ_DIR)/ll_table.o $(OBJ_DIR)/rand_util.o


# dependencies between .o files and .cc (or .c) files

$(OBJ_DIR)/gqf.o:						$(LOC_SRC)/gqf.c $(LOC_INCLUDE)/gqf.h
$(OBJ_DIR)/gqf_file.o:					$(LOC_SRC)/gqf_file.c $(LOC_INCLUDE)/gqf_file.h
$(OBJ_DIR)/hashutil.o:					$(LOC_SRC)/hashutil.c $(LOC_INCLUDE)/hashutil.h
$(OBJ_DIR)/partitioned_counter.o:		$(LOC_INCLUDE)/partitioned_counter.h
$(OBJ_DIR)/ll_table.o:					$(LOC_SRC)/ll_table.c $(LOC_INCLUDE)/ll_table.h
$(OBJ_DIR)/splinter_util.o:				$(LOC_SRC)/splinter_util.c $(LOC_INCLUDE)/splinter_util.h# $(OBJ_DIR)/gqf.o $(OBJ_DIR)/gqf_file.o
$(OBJ_DIR)/test_driver.o:				$(LOC_SRC)/test_driver.c $(LOC_INCLUDE)/test_driver.h# $(OBJ_DIR)/gqf.o $(OBJ_DIR)/gqf_file.o $(OBJ_DIR)/splinter_util.o
$(OBJ_DIR)/bench_variants.o:				$(LOC_BENCH)/bench_variants.cc
$(OBJ_DIR)/workload_gen.o:				$(LOC_BENCH)/workload_gen.cc $(LOC_SRC)/rand_util.c

#
# generic build rules
#

$(SPLTARGETS): | BUILD_DIR
	$(LD) $^ -o $(BUILD_DIR)/$@ $(LDFLAGS)

$(CXXTARGETS): 
	$(CXX) $^ -o $(BUILD_DIR)/$@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(LOC_SRC)/%.cc | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDE) $< -c -o $@

$(OBJ_DIR)/%.o: $(LOC_SRC)/%.c | $(OBJ_DIR)
	$(CC) $(CXXFLAGS) $(INCLUDE) $< -c -o $@

$(OBJ_DIR)/%.o: $(LOC_BENCH)/%.c | $(OBJ_DIR)
	$(CC) $(CXXFLAGS) $(INCLUDE) $< -c -o $@

$(OBJ_DIR)/%.o: $(LOC_BENCH)/%.cc | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $(INCLUDE) $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(OBJ_DIR) $(CXXTARGETS) $(SPLTARGETS) core
	rm -rf *.csv 
	rm -rf rm
	rm -rf reverseMap
	rm -rf out
	rm -rf output.txt
	rm -rf database
	rm -rf *_wiredTiger
	rm -rf $(BUILD_DIR)
