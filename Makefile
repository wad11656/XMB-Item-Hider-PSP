TARGET = xmbih
OBJS = main.o minIni.o

MININI_DEFINES = -DNDEBUG -DINI_READONLY -DINI_FILETYPE=SceUID -DPORTABLE_STRNICMP -DINI_NOFLOAT
CFLAGS = -Os -G0 -Wall -std=gnu99 -fshort-wchar -fno-pic -mno-check-zero-division $(MININI_DEFINES) -DKPRINTF_ENABLED -DCONFIG_6xx=1
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PSP_FW_VERSION = 660

INCDIR = include
LIBDIR = lib

# -nodefaultlibs prevents psp-gcc's default specs from auto-linking newlib's
# libc (which drags in __sinit, __retarget_lock_*, _ctype_, etc., causing
# Kernel_Library / ThreadManForUser / sceNetInet imports that the PSP module
# loader rejects for a user-mode plugin).
# -Wl,-T,discard.ld supplements the SDK's linker script with /DISCARD/ entries
# for sections modern binutils emits that the PSP loader can't handle
# (.MIPS.abiflags assigns itself an LMA that collides with .text).
LDFLAGS = -nodefaultlibs -Wl,-T,discard.ld

LIBS = -Llib -lpspsystemctrl_user -lpspkubridge -lpspvshctrl -lpspsystemctrl_kernel -lpspuser -lpspmodinfo -lgcc

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak

# Drop a copy of the built PRX into release/ so it's the single canonical
# location for the ready-to-flash binary (and the one .prx that's tracked
# in git per .gitignore's `!release/*.prx` rule).
all: install-release
.PHONY: install-release
install-release: $(TARGET).prx
	@mkdir -p release
	cp -f $(TARGET).prx release/$(TARGET).prx
