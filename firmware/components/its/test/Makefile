# Alternative to CMake:  make -C firmware/components/its/test test
CC      ?= gcc
SAN     ?= -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS  += -std=gnu99 -Wall -Wextra -Wpedantic -Wshadow -Werror -g -O1 $(SAN) -I../include -I../src -I.
SRCS     = test_main.c ../src/uper.c ../src/its_frame.c ../src/its_cam.c ../src/its_denm.c \
           ../src/its_mapspat.c ../src/its_names.c ../src/its_store.c

its_test: $(SRCS) vectors.h
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lm $(SAN)

vectors.h: gen_vectors.py asn1/dsrc-subset.asn
	python3 -I gen_vectors.py

.PHONY: test regen clean
test: its_test
	./its_test

regen:
	python3 -I gen_vectors.py

clean:
	rm -f its_test
