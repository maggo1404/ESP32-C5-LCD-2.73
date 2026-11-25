# Host test for the battery curve / guard (no ESP-IDF needed).
CC ?= gcc
CFLAGS = -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all
test_battery: test_battery.c ../battery.c ../battery.h
	$(CC) $(CFLAGS) -o $@ test_battery.c ../battery.c -lm
.PHONY: test clean
test: test_battery
	./test_battery
clean:
	rm -f test_battery
.DEFAULT_GOAL := test
