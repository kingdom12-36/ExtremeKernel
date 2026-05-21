ifndef TARGET_COMPILE
$(error TARGET_COMPILE is not set)
endif

ifndef KP_DIR
    KP_DIR = ../..
endif

CC = $(TARGET_COMPILE)gcc
LD = $(TARGET_COMPILE)ld

INCLUDE_DIRS := . include patch/include linux/include linux/arch/arm64/include linux/tools/arch/arm64/include
INCLUDE_FLAGS := $(foreach dir,$(INCLUDE_DIRS),-I$(KP_DIR)/kernel/$(dir))

objs := extreme-tweaker.o

all: extreme-tweaker.kpm

extreme-tweaker.kpm: $(objs)
	$(CC) -r -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -Tkpm.lds -c -O2 -o $@ $<

.PHONY: clean
clean:
	rm -f *.kpm *.o
