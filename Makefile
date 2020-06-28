TOPDIR = $(realpath ../..)

PROGRAM = console
SOURCES = main.c console.c tty.c event.c filesys.c sysent.c proc.c trampoline.S utils.c
SOURCES_GEN = data/drdos8x8.c data/koi8r.8x8.c data/lat2-08.c data/pointer.c \
	      data/screen.c
OBJECTS = ../startup.o ../fault.o ../trap.o ushell.exe.o ucat.exe.o

CLEAN-FILES = crt0.o sysapi.o shell.o cat.o
CLEAN-FILES += ushell.bin.o ushell.exe ushell.elf ushell.elf.map
CLEAN-FILES += ucat.bin.o ucat.exe ucat.elf ucat.elf.map

LAUNCHOPTS = --window serial
ADF-EXTRA = adf-extra/*.txt ucat.exe ushell.exe

PSF2C.drdos8x8 := --name console
PSF2C.koi8r.8x8 := --name console
PSF2C.lat2-08 := --name console
PNG2C.pointer := --sprite pointer_spr,16,1 --palette pointer_pal,4

include $(TOPDIR)/build/build.prog.mk

data/screen.c:
	$(GENSTRUCT) --bitmap 640x256x1 screen > $@

u%.elf: crt0.o %.o sysapi.o $(TOPDIR)/libc/c.lib
	@echo "[LD] $(addprefix $(DIR), $^) -> $(DIR)$@"
	$(LD) $(LDFLAGS) -Map $@.map -o $@ $^
