# Build 7zSfxSF.sfx gui, console, and their admin versions for x64 or x86
# Tested on VS2026 nmake.
# Public domain, by chrdev.


.SUFFIXES : .c .asm .rc .def


# Target paths
# =============================================================================

B = build
O = $B\$(PLATFORM)
R = $B\release\$(PLATFORM)
SFX = $O\7zSf.exe $O\7zSf_admin.exe $O\7zSf_con.exe $O\7zSf_con_admin.exe


# Source files
# =============================================================================

7Z_OBJS = \
	$O\7zAlloc.obj \
	$O\7zArcIn.obj \
	$O\7zBuf.obj \
	$O\7zBuf2.obj \
	$O\7zFile.obj \
	$O\7zDec.obj \
	$O\7zStream.obj \
	$O\Bcj2.obj \
	$O\Bra.obj \
	$O\Bra86.obj \
	$O\BraIA64.obj \
	$O\CpuArch.obj \
	$O\Delta.obj \
	$O\DllSecur.obj \
	$O\Lzma2Dec.obj \
	$O\LzmaDec.obj \
	$O\7zCrc.obj \

7Z_ASM_OBJS = \
	$O\7zCrcOpt.obj \

!IF "$(PLATFORM)" == "x86"
C_ASM_OBJS = \
	$O\llshl.obj \
	$O\ullshr.obj \
!ENDIF

LIB_OBJS = \
	$(7Z_OBJS) \
	$(7Z_ASM_OBJS) \
	$O\msvcrt.lib \
!IF "$(PLATFORM)" == "x86"
	$(C_ASM_OBJS) \
!ENDIF

SFX_OBJS = \
	$O\SfxSF.obj \
	$O\resource.res \

SFX_OBJS_CON = \
	$O\SfxSF_con.obj \
	$O\resource_con.res \


# Builders and flags
# =============================================================================

CFLAGS = $(CFLAGS) /nologo \
	/c /Wall /WX /Gy /GF -GS- -Zc:wchar_t \
	/O1 \
	/DUNICODE /D_UNICODE \
	/DZ7_EXTRACT_ONLY \

AFLAGS = $(AFLAGS) /nologo /c /WX

RFLAGS = $(RFLAGS) /nologo
!IF "$(MY_VER_FIX)" != ""
RFLAGS = $(RFLAGS) /dMY_VER_FIX=$(MY_VER_FIX)
!ENDIF

LD = link

LDLIBS = Kernel32.lib user32.lib shell32.lib

LDFLAGS = $(LDFLAGS) /NOLOGO \
	/FIXED \
	/WX \
	/NODEFAULTLIB \
	/ENTRY:entry \
	/MANIFEST:EMBED \
	/MANIFESTINPUT:dpiAware.manifest \

!IF "$(PLATFORM)" == "x86"
# Subsystem 5.01 runs on Windows XP 32-bit
LDFLAGS_SUBSYS = /SUBSYSTEM:WINDOWS,5.01
LDFLAGS_SUBSYS_CON = /SUBSYSTEM:CONSOLE,5.01
!ELSE
# Subsystem 5.02 runs on Windows XP 64-bit
LDFLAGS_SUBSYS = /SUBSYSTEM:WINDOWS,5.02
LDFLAGS_SUBSYS_CON = /SUBSYSTEM:CONSOLE,5.02
!ENDIF

LDFLAGS_ADMIN = \
	/MANIFESTUAC:NO \
	/MANIFESTINPUT:admin.manifest \

LINK = $(LD) /OUT:$@ $** $(LDFLAGS) $(LDFLAGS_SUBSYS) $(LDLIBS)
LINK_CON = $(LD) /OUT:$@ $** $(LDFLAGS) $(LDFLAGS_SUBSYS_CON) $(LDLIBS)


# Dependenies
# =============================================================================

all : $O $(SFX)

$O :
	if not exist "$O" mkdir "$O"

$O\7zSf.exe : $(LIB_OBJS) $(SFX_OBJS)
	$(LINK)

$O\7zSf_admin.exe : $(LIB_OBJS) $(SFX_OBJS)
	$(LINK) $(LDFLAGS_ADMIN)
	
$O\7zSf_con.exe : $(LIB_OBJS) $(SFX_OBJS_CON)
	$(LINK_CON)

$O\7zSf_con_admin.exe : $(LIB_OBJS) $(SFX_OBJS_CON)
	$(LINK_CON) $(LDFLAGS_ADMIN)


$(7Z_OBJS) : ../../$(*B).c
$(7Z_ASM_OBJS) : ../../../Asm/x86/$(*B).asm
$O\msvcrt.lib : $(*B).def
$O\SfxSF.obj : $(*B).c
$O\resource.res : $(*B).rc

$O\SfxSF_con.obj : SfxSF.c
	$(CC) $(CFLAGS) /D_CONSOLE /Fo$@ $?

$O\resource_con.res : resource.rc
	$(RC) $(RFLAGS) /d_CONSOLE /fo$@ $?

!IF "$(PLATFORM)" == "x86"
# $(VCTOOLSINSTALLDIR) probably contains spaces, so no inference rule, sucks
$(C_ASM_OBJS) : "$(VCTOOLSINSTALLDIR)crt\src\i386\$(*B).asm"
	$(AS) $(AFLAGS) /Fo$O\ $?
!ENDIF


# Inferences
# =============================================================================

{..\..}.c{$O}.obj::
	$(CC) $(CFLAGS) /Fo$O\ $<

{..\..\..\Asm\x86}.asm{$O}.obj::
	$(AS) $(AFLAGS) /Fo$O\ $<

{}.def{$O}.lib:
	lib /nologo /MACHINE:$(PLATFORM) /OUT:$@ /DEF:$<

{}.c{$O}.obj:
	$(CC) $(CFLAGS) /Fo$@ $<

{}.rc{$O}.res:
	$(RC) $(RFLAGS) /fo$@ $<


# Maintenance
# =============================================================================

clean:
	if exist "$O" rmdir /s /q "$O"

cleanall:
	if exist "$B" rmdir /s /q "$B"
