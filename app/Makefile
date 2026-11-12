# TunnelDPI - MinGW-w64 ile derleme
#   Windows (WinLibs/MSYS2):  mingw32-make
#   Linux çapraz derleme:     make CPREFIX=x86_64-w64-mingw32-
CPREFIX ?=
CC      = $(CPREFIX)gcc
WINDRES = $(CPREFIX)windres

TARGET  = TunnelDPI.exe
SRCS    = $(wildcard src/*.c)
OBJS    = $(SRCS:.c=.o) res/app.o

CFLAGS  = -std=c11 -Os -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
          -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -D__USE_MINGW_ANSI_STDIO=0 \
          -ffunction-sections -fno-ident -fno-asynchronous-unwind-tables
LDFLAGS = -municode -mwindows -s -static -Wl,--gc-sections -Wl,--dynamicbase -Wl,--nxcompat \
          -Wl,--high-entropy-va
LDLIBS  = -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 -ladvapi32 -liphlpapi -lsecur32 \
          -ldwmapi -luxtheme

all: $(TARGET)

# GCC her exe'ye kendi varsayılan manifestini (default-manifest.o) ekler ve bu
# bizimkiyle çakışır. -B ile önce boş bir default-manifest.o bulunmasını sağlıyoruz.
$(TARGET): $(OBJS) res/nomanifest/default-manifest.o
	$(CC) -Bres/nomanifest/ $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

res/nomanifest/default-manifest.o: res/nomanifest/default-manifest.c
	$(CC) -c -o $@ $<

src/%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) -c -o $@ $<

res/app.o: res/app.rc res/app.manifest res/icon.ico res/resource.h src/version.h
	$(WINDRES) -c 65001 -O coff -i $< -o $@

# Kurulum: önce dist/ hazırlanıp setup/payload.bin üretilmeli (build.cmd yapar):
#   python setup/pack.py dist setup/payload.bin && make setup
SETUP = TunnelDPI-Setup.exe

setup: $(SETUP)

$(SETUP): setup/setup.o setup/setup_res.o res/nomanifest/default-manifest.o
	$(CC) -Bres/nomanifest/ $(LDFLAGS) -o $@ setup/setup.o setup/setup_res.o 		-lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 -luser32 -lgdi32

setup/setup.o: setup/setup.c setup/wizard.h src/version.h
	$(CC) $(CFLAGS) -c -o $@ $<

setup/setup_res.o: setup/setup.rc setup/setup.manifest setup/payload.bin res/icon.ico src/version.h
	$(WINDRES) -c 65001 -O coff -I setup -i $< -o $@

clean:
	-rm -f src/*.o res/*.o res/nomanifest/*.o setup/*.o $(TARGET) $(SETUP)

.PHONY: all clean setup
