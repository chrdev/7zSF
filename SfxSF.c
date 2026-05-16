/* SfxSF.c - 7z Special Folder SFX
*  Modified from SfxSetup(7zS2.sfx), LZMA-SDK
*  2026-05-02 : Public domain, chrdev
*/

#include "Precomp.h"
#include "../../7z.h"
#include "../../7zAlloc.h"
#include "../../7zCrc.h"
#include "../../7zFile.h"
#include "../../CpuArch.h"
#include "../../DllSecur.h"
#include <shlobj.h> // SHGetFolderPath()


#define wcscat lstrcatW
#define wcslen (size_t)lstrlenW
#define wcscpy lstrcpyW
#define strlen (size_t)lstrlenA


// Return error message.
// If successful, error message is NULL.
static Z7_FORCE_INLINE const char*
parseCmdline(void) {
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv) LocalFree(argv);
	if (argc > 1) return "This installer runs without args";
	return NULL;
}

#ifdef _CONSOLE
static BOOL WINAPI
handleConsoleCtrl(DWORD type) {
	UNUSED_VAR(type);
	return TRUE;
}
#endif

static Z7_FORCE_INLINE BoolInt
findSignature(const Byte* fv, UInt64* pos, UInt64 searchLimit) {
	for (const Byte* p = fv; p <= fv + searchLimit - k7zStartHeaderSize; ++p) {
		if (*p != '7') continue;
		if (memcmp(p, k7zSignature, k7zSignatureSize)) continue;
		if (CrcCalc(p + 12, 20) == GetUi32(p + 8)) {
			*pos = p - fv;
			return True;
		}
	}
	return False;
}

// Return error message.
// If successful, error message is NULL.
static Z7_FORCE_INLINE const char*
openSfxFile(CSzFile* f) {
	static const size_t kSignatureSearchLimit = (1 << 22);

	const char* errMsg = NULL;
	WCHAR sfxPath[MAX_PATH];
	Byte* fileView = NULL;

#ifdef _DEBUG
	wcscpy(sfxPath, L"R:\\payload.7z");
#else
	DWORD dwRet = GetModuleFileNameW(NULL, sfxPath, MAX_PATH);
	if (dwRet == 0 || dwRet >= MAX_PATH) return "Can't get sfx path";
#endif
	if (InFile_OpenW(f, sfxPath)) return "Can't open sfx file";

	UInt64 searchLimit;
	if (File_GetLength(f, &searchLimit)) {
		errMsg = "Can't get file size";
		goto fin;
	}
	if (searchLimit > kSignatureSearchLimit) searchLimit = kSignatureSearchLimit;
	HANDLE h = CreateFileMappingW(f->handle, NULL, PAGE_READONLY, 0, (DWORD)searchLimit, NULL);
	if (!h) {
		errMsg = "Can't create sfx file mapping";
		goto fin;
	}
	fileView = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
	CloseHandle(h);
	if (!fileView) {
		errMsg = "Can't map view of sfx file";
		goto fin;
	}

	UInt64 pos = 0;
	if (!findSignature(fileView, &pos, searchLimit)) {
		errMsg = "Can't find 7z payload";
		goto fin;
	}

	if (File_Seek(f, (Int64*)&pos, SZ_SEEK_SET)) {
		errMsg = "Can't seek file";
		//goto fin;
	}

fin:
	if (fileView) UnmapViewOfFile(fileView);
	if (errMsg) File_Close(f);
	return errMsg;
}

static const char*
get7zErrmsg(SRes code) {
	switch (code) {
	case SZ_ERROR_UNSUPPORTED:
		return "Decoder doesn't support this archive";
	case SZ_ERROR_MEM:
		return "Can't allocate memory";
	case SZ_ERROR_CRC:
		return "CRC error";
	default:
		return "ERROR";
	}
}

typedef struct Z7Context {
	CFileInStream arStream;
	ISzAlloc allocImp;
	ISzAlloc allocTempImp;
	CLookToRead2 lookStream;

	CSzArEx db;
	UInt32 blockIndex;       /* index of solid block */
	Byte* outBuffer;         /* pointer to pointer to output buffer (allocated with allocMain) */
	size_t outBufferSize;    /* buffer size for output buffer */
	size_t offset;           /* offset of stream for required file in *outBuffer */
	size_t outSizeProcessed; /* size of file in *outBuffer */
}Z7Context;

static void
closeAr(Z7Context* c) {
	ISzAlloc_Free(&c->allocImp, c->outBuffer);
	SzArEx_Free(&c->db, &c->allocImp);
	ISzAlloc_Free(&c->allocImp, c->lookStream.buf);

	File_Close(&c->arStream.file);
}

// Return error message.
// If successful, error message is NULL.
// If failed, file is closed.
static Z7_FORCE_INLINE const char*
openAr(Z7Context* c) {
	const char* errmsg = NULL;
	static const size_t kInputBufSize = (size_t)1 << 18;

	c->allocImp.Alloc = SzAlloc;
	c->allocImp.Free = SzFree;
	c->allocTempImp.Alloc = SzAllocTemp;
	c->allocTempImp.Free = SzFreeTemp;

	c->outBuffer = 0;

	FileInStream_CreateVTable(&c->arStream);
	LookToRead2_CreateVTable(&c->lookStream, False);
	SzArEx_Init(&c->db);

	c->lookStream.buf = (Byte*)ISzAlloc_Alloc(&c->allocImp, kInputBufSize);
	if (!c->lookStream.buf) {
		errmsg = get7zErrmsg(SZ_ERROR_MEM);
		goto fin;
	}

	c->lookStream.bufSize = kInputBufSize;
	c->lookStream.realStream = &c->arStream.vt;
	LookToRead2_INIT(&c->lookStream)
	SRes res = SzArEx_Open(&c->db, &c->lookStream.vt, &c->allocImp, &c->allocTempImp);
	if (res != SZ_OK) {
		errmsg =  get7zErrmsg(res);
		// goto fin;
	}
fin:
	if (errmsg) closeAr(c);
	return errmsg;
}

static Z7_FORCE_INLINE SRes
extractOne(Z7Context* c, UInt32 index) {
	return SzArEx_Extract(&c->db, &c->lookStream.vt,
		index,
		&c->blockIndex, &c->outBuffer, &c->outBufferSize,
		&c->offset, &c->outSizeProcessed,
		&c->allocImp, &c->allocTempImp);
}

// Return True if user input yes.
// Return False if user input no.
static Z7_FORCE_INLINE BoolInt
getYesNo(const Byte* msgData, size_t cb) {
	if (cb < 2 || cb % 2) return True;
	if (msgData[0] != 0xFF || msgData[1] != 0xFE) return True;

	WCHAR msg[1024];
	if (cb > sizeof(msg)) cb = sizeof(msg);
	memcpy(msg, msgData + 2, cb - 2);
	const size_t cch = cb / 2;
	msg[cch - 1] = L'\0';

#ifdef _CONSOLE
	static const char kYN[] = "(Y/N)\n";
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if (h == INVALID_HANDLE_VALUE) return True;

	WriteConsoleW(h, msg, (DWORD)cch - 1, NULL, NULL);
	WriteConsoleA(h, kYN, sizeof(kYN) - 1, NULL, NULL);

	char buf[8];
	DWORD cbBuf;
	if (!ReadConsoleA(GetStdHandle(STD_INPUT_HANDLE), buf, sizeof(buf), &cbBuf, NULL)) return True;
	if (cbBuf > 2 && (buf[0] | 0x20) == 'y') return True;
	return False;
#else
	return IDYES == MessageBoxW(NULL, msg, L"7zSF", MB_YESNO);
#endif
}

// Return error message.
// If successful, error message is NULL.
// If failed, or user input no, file is closed.
static Z7_FORCE_INLINE const char*
mayExtract(Z7Context* c, BoolInt* yes) {
	*yes = True;
	const char* errmsg = NULL;
	for (UInt32 i = c->db.NumFiles - 1; i != (UInt32)-1; --i) {
		const size_t offs = c->db.FileNameOffsets[i];
		const size_t cch = c->db.FileNameOffsets[i + 1] - offs;
		const WCHAR* name = (const WCHAR*)(c->db.FileNames + offs * 2);
		if (cch != 2) continue;
		if (*name != L'y' && *name != L'Y') continue;

		if (SzArEx_IsDir(&c->db, i)) {
			errmsg = "y (prompt) must be a file";
			break;
		}
		SRes res = extractOne(c, i);
		if (res != SZ_OK) {
			errmsg = get7zErrmsg(res);
			break;
		}

		*yes = getYesNo(c->outBuffer + c->offset, c->outSizeProcessed);
		break;
	}
	if (errmsg || !*yes) closeAr(c);
	return errmsg;
}

static Z7_FORCE_INLINE BoolInt
isBlankWChar(WCHAR c) {
	switch (c) {
	case L' ':
	case L'\t':
	case L'\r':
	case L'\n':
		return True;
	default:
		return False;
	}
}

// Parsed istall command is meant to be fed to CreateProcess().
// Caller should free the return value.
static Z7_FORCE_INLINE WCHAR*
parseInstCmd(const Byte* z, size_t cb) {
	WCHAR* rlt = NULL;

	if (cb % 2 || cb < 4) return NULL;
	if (z[0] != 0xFF || z[1] != 0xFE) return NULL; // Detect UTF16-LE BOM.

	WCHAR* line = malloc(cb);
	if (!line) return NULL;
	memcpy(line, z + 2, cb - 2);
	size_t cch = cb / 2 - 2;
	for (; isBlankWChar(line[cch]); --cch) {
		if (!cch) goto fin;
	}
	line[cch + 1] = L'\0';

	cch = ExpandEnvironmentStrings(line, NULL, 0);
	if (!cch) goto fin;
	rlt = malloc(sizeof(*rlt) * cch);
	if (!rlt) goto fin;
	ExpandEnvironmentStrings(line, rlt, (DWORD)cch);

fin:
	free(line);
	return rlt;
}

static Z7_FORCE_INLINE void
createDir(const WCHAR* path) {
	CreateDirectoryW(path, NULL);
}

// If failed, return -1.
static Z7_FORCE_INLINE int
parseHexWChar(WCHAR c) {
	if (c <= L'9' && c >= L'0') return (int)(c - L'0');
	if (c <= L'F' && c >= L'A') return (int)(c - L'A') + 0xA;
	if (c <= L'f' && c >= L'a') return (int)(c - L'a') + 0xA;
	return -1;
}

// s is expected to be a 2-digits-hex, followed by a L'/' or NUL.
// If s format is wrong, or parse failed, return -1.
static Z7_FORCE_INLINE int
csidlFromWcs(const WCHAR* s) {
	int rlt = 0;
	for (int i = 0; i < 2; ++i) {
		int v = parseHexWChar(*s++);
		if (v < 0) return -1;
		rlt <<= 4;
		rlt |= v;
	}
	switch (*s) {
	case L'/':
	case L'\0':
		return rlt;
	default:
		return -1;
	}
}

static Z7_FORCE_INLINE BoolInt
shGetPath(WCHAR* path, int csidl) {
	return SUCCEEDED(SHGetFolderPathW(NULL, csidl | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, path));
}

// Return error message.
// If successful, error message is NULL.
static Z7_FORCE_INLINE const char*
extractAndClose(Z7Context* c, WCHAR** instCmd) {
	const char* errmsg = NULL;

	int lastId = -1;
	WCHAR path[MAX_PATH * 2];
	size_t rootLen = 0;
	for (UInt32 index = 0; index < c->db.NumFiles; ++index) {
		WCHAR pathInAr[MAX_PATH];

		if (SzArEx_GetFileNameUtf16(&c->db, index, NULL) >= MAX_PATH) {
			errmsg = "Path in archive is too long";
			break;
		}
		SzArEx_GetFileNameUtf16(&c->db, index, (UInt16*)pathInAr);
		if (!pathInAr[1] && (pathInAr[0] == L'y' || pathInAr[0] == L'Y')) continue;

		SRes res = extractOne(c, index);
		if (res != SZ_OK) {
			errmsg = get7zErrmsg(res);
			break;
		}

		if (!pathInAr[1] && (pathInAr[0] == L'z' || pathInAr[0] == L'Z')) {
			if (SzArEx_IsDir(&c->db, index)) {
				errmsg = "z (install command) must be a file";
				break;
			}
			*instCmd = parseInstCmd(c->outBuffer + c->offset, c->outSizeProcessed);
			continue;
		}
		int id = csidlFromWcs(pathInAr);
		if (id < 0) {
			errmsg = "Path must be CSIDL";
			break;
		}
		if (lastId != id) {
			lastId = id;
			if (!shGetPath(path, id)) {
				errmsg = "Path must be valid CSIDL";
				break;
			}
			rootLen = wcslen(path);
			path[rootLen++] = L'\\';
		}

		if (!pathInAr[2]) continue; // We don't need to do anything about a CSIDL root path.

		WCHAR* subPath = path + rootLen;
		wcscpy(subPath, pathInAr + 3);

		for (; *subPath; ++subPath) {
			if (*subPath != L'/') continue;
			*subPath = L'\0';
			createDir(path);
			*subPath = L'\\';
		}
		if (SzArEx_IsDir(&c->db, index)) {
			createDir(path);
			continue;
		}

		CSzFile outFile;
		if (OutFile_OpenW(&outFile, path)) {
			errmsg = "Can't open output file";
			break;
		}

		size_t cb = c->outSizeProcessed;
		if (File_Write(&outFile, c->outBuffer + c->offset, &cb) != 0 || cb != c->outSizeProcessed) {
			errmsg = "Can't write output file";
			break;
		}

		if (SzBitWithVals_Check(&c->db.MTime, index)) {
			const CNtfsFileTime* t = c->db.MTime.Vals + index;
			FILETIME mTime;
			mTime.dwLowDateTime = t->Low;
			mTime.dwHighDateTime = t->High;
			SetFileTime(outFile.handle, NULL, NULL, &mTime);
		}

		const WRes wres = File_Close(&outFile);
		if (wres != 0) {
			errmsg = "Can't close output file";
			break;
		}

		if (SzBitWithVals_Check(&c->db.Attribs, index)) {
			SetFileAttributesW(path, c->db.Attribs.Vals[index]);
		}
	}
	closeAr(c);
	return errmsg;
}

static Z7_FORCE_INLINE void
runInstCmd(WCHAR* cmd) {
	STARTUPINFOW si = { .cb = sizeof(si) };
	PROCESS_INFORMATION pi;
	if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

static Z7_FORCE_INLINE void
showError(const char* s) {
#ifdef _CONSOLE
	static const char k7z[] = "7zSF: ";
	HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
	if (h == INVALID_HANDLE_VALUE) return;
	WriteConsoleA(h, k7z, sizeof(k7z) - 1, NULL, NULL);
	WriteConsoleA(h, s, strlen(s), NULL, NULL);
	WriteConsoleA(h, "\n", 1, NULL, NULL);
#else
	MessageBoxA(NULL, s, "7zSF Error", MB_ICONERROR);
#endif
}

void entry(void) {
	LoadSecurityDlls();
	const char* errmsg;

	// User may issue "/?". We give feedback rather than install.
	if ((errmsg = parseCmdline())) goto err;
	
#ifdef _CONSOLE
	SetConsoleCtrlHandler(handleConsoleCtrl, TRUE);
#endif

	CrcGenerateTable();

	Z7Context ctx;
	if ((errmsg = openSfxFile(&ctx.arStream.file))) goto err;
	if ((errmsg = openAr(&ctx))) goto err;

	BoolInt yes;
	if ((errmsg = mayExtract(&ctx, &yes))) goto err;
	if (!yes) ExitProcess(0);

	WCHAR* instCmd = NULL;
	if ((errmsg = extractAndClose(&ctx, &instCmd))) goto err;
	if (instCmd) runInstCmd(instCmd);
	ExitProcess(0);

err:
	showError(errmsg);
	ExitProcess(1);
}
