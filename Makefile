CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -pedantic
LDLIBS := -lpng -ljpeg

all: scrubimg

scrubimg: scrubimg.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f scrubimg
