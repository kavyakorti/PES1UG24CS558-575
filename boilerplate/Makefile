obj-m += monitor.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

CC=gcc
CFLAGS=-Wall -Wextra -pthread

all: engine cpu_hog io_pulse memory_hog kernel

engine: engine.c monitor_ioctl.h
	$(CC) $(CFLAGS) -o engine engine.c

cpu_hog: cpu_hog.c
	$(CC) -Wall -Wextra -o cpu_hog cpu_hog.c

io_pulse: io_pulse.c
	$(CC) -Wall -Wextra -o io_pulse io_pulse.c

memory_hog: memory_hog.c
	$(CC) -Wall -Wextra -o memory_hog memory_hog.c

kernel:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

ci: engine cpu_hog io_pulse memory_hog

clean:
	rm -f engine cpu_hog io_pulse memory_hog *.o *.ko *.mod.* Module.symvers modules.order
