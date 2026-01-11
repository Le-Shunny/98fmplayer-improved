vpath %.c ../
vpath %.c ../../fmdriver
vpath %.c ../../libopna
vpath %.c ../../fmdsp
vpath %.c ../../tonedata
vpath %.c ../../common
vpath %.c ../../fft
vpath %.c ../../pacc
vpath %.rc ..

include ../fmplayer.mak

ARCH=x86_64
CC=gcc
WINDRES=windres
STRIP=strip

CFLAGS=-std=c99 -O2 -Wall -Wextra -pedantic -I../.. $(addprefix -D,$(DEFINES))
SSECFLAGS=-mssse3 -O3
LIBS=-mwindows -municode $(addprefix -l,$(LIBBASE))
VERSION=`dd bs=6 status=none if=../../versionprint.txt`

MANIFEST=../lnf.manifest
RESOBJ=lnf.o

OBJS=$(addsuffix .o,$(OBJBASE) $(RESBASE)) $(RESOBJ)
OBJS+=$(addsuffix .sse.o,$(SSEOBJBASE))

TARGET=98fmplayer.exe

$(TARGET): $(OBJS)
	@echo "  CCLD     $@"
	@$(CC) -o $@ $(OBJS) $(LIBS)
	@$(STRIP) $@

%.o: %.c
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) -c $< -o $@

%.sse.o: %.c
	@echo "  CC       $@"
	@$(CC) $(CFLAGS) $(SSECFLAGS) -c $< -o $@

lnf.o: ../lnf.rc $(MANIFEST)
	@echo "  WINDRES  $@"
	@$(WINDRES) -i ../lnf.rc -o $@

../lnf.manifest: ../lnf.manifest.in ../../versionprint.txt
	@echo "  VERSION  $(VERSION)"
	@sed -e "s/@VER@/$(VERSION)/g" $< > $@

clean:
	rm -f $(TARGET) $(OBJS) ../lnf.manifest
