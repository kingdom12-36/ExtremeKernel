ifndef TARGET_COMPILE
$(error TARGET_COMPILE is not set)
endif

ifndef KP_DIR
    KP_DIR = ../..
endif

CC = $(TARGET_COMPILE)gcc
LD = $(TARGET_COMPILE)ld

# Include paths needed for kernel headers
INCLUDE_DIRS := . include patch/include linux/include linux/arch/arm64/include
INCLUDE_FLAGS := $(foreach dir,$(INCLUDE_DIRS),-I$(KP_DIR)/kernel/$(dir))

# Required flags for kernel module compilation
CFLAGS += -D__KERNEL__ -DCONFIG_KALLSYMS
CFLAGS += -fno-pic -fno-pie -mgeneral-regs-only

objs := disabler.o

all: disabler.kpm

disabler.kpm: $(objs)
	$(CC) -r -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE_FLAGS) -c -O2 -o $@ $<

.PHONY: clean
clean:
	rm -f *.kpm *.o
