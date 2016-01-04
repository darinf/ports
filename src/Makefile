# vim: noet ts=8 sw=8

GTEST_DIR = ../../googletest/googletest

SOURCES = \
	logging.cc \
	message.cc \
	message_queue.cc \
	node_impl.cc \
	node.cc \
	port.cc \
	port_ref.cc \
	$(GTEST_DIR)/src/gtest_main.cc \
	$(NULL)

OBJECTS = $(patsubst %.cc,%.o,$(SOURCES))
CPPFLAGS = -std=c++11 -pthread -g -Wall -I ../.. -isystem $(GTEST_DIR)/include
LDFLAGS = -pthread
CC = g++

all: test threaded_stress_test

test: test.o $(OBJECTS) $(GTEST_DIR)/libgtest.a
threaded_stress_test: threaded_stress_test.o $(OBJECTS) $(GTEST_DIR)/libgtest.a

.PHONY: clean
clean:
	rm -f *.o test threaded_stress_test
