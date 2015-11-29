# vim: noet ts=8 sw=8

SOURCES = \
	message.cc \
	message_queue.cc \
	node_impl.cc \
	node.cc \
	test.cc \
	$(NULL)

OBJECTS = $(patsubst %.cc,%.o,$(SOURCES))
CPPFLAGS = -std=c++11
CC = c++

test: test.o $(OBJECTS)

.PHONY: clean
clean:
	rm *.o
