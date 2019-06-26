CFLAGS = -Wall
OBJS = dma.o mailbox.o main.o pcm.o pwm.o rpihw.o ws2811.o

all: leds

install: all
	cp leds /usr/local/bin/
	cp cgi /usr/lib/cgi-bin/leds

leds: $(OBJS)
	$(CC) -o $@ $^

clean:
	rm -f $(OBJS) leds
