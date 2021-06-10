# Makefile for pagemap

CC = g++
CFLAGS = -std=c++98

.PHONY: all
all: mem_tracker

mem_tracker: mem_tracker.cpp
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	-rm mem_tracker
