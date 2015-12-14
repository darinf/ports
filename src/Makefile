# vim: noet ts=8 sw=8

GTEST_DIR = ../../googletest/googletest

SOURCES = \
	message.cc \
	message_queue.cc \
	node_impl.cc \
	node.cc \
	test.cc \
	$(GTEST_DIR)/src/gtest_main.cc \
	$(NULL)

OBJECTS = $(patsubst %.cc,%.o,$(SOURCES))
CPPFLAGS = -std=c++11 -pthread -g -Wall -isystem $(GTEST_DIR)/include
LDFLAGS = -pthread
CC = g++

test: test.o $(OBJECTS) $(GTEST_DIR)/libgtest.a

.PHONY: clean
clean:
	rm *.o test
