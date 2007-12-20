#include "precompiled.h"
#include "wmman.h"

#include "wposix_internal.h"
#include "crt_posix.h"		// _get_osfhandle


//-----------------------------------------------------------------------------
// memory mapping
//-----------------------------------------------------------------------------

// convert POSIX PROT_* flags to their Win32 PAGE_* enumeration equivalents.
// used by mprotect.
static DWORD win32_prot(int prot)
{
	// this covers all 8 combinations of read|write|exec
	// (note that "none" means all flags are 0).
	switch(prot & (PROT_READ|PROT_WRITE|PROT_EXEC))
	{
	case PROT_NONE:
		return PAGE_NOACCESS;
	case PROT_READ:
		return PAGE_READONLY;
	case PROT_WRITE:
		// not supported by Win32; POSIX allows us to also grant read access.
		return PAGE_READWRITE;
	case PROT_EXEC:
		return PAGE_EXECUTE;
	case PROT_READ|PROT_WRITE:
		return PAGE_READWRITE;
	case PROT_READ|PROT_EXEC:
		return PAGE_EXECUTE_READ;
	case PROT_WRITE|PROT_EXEC:
		// not supported by Win32; POSIX allows us to also grant read access.
		return PAGE_EXECUTE_READWRITE;
	case PROT_READ|PROT_WRITE|PROT_EXEC:
		return PAGE_EXECUTE_READWRITE;
	NODEFAULT;
	}

	UNREACHABLE;
}


int mprotect(void* addr, size_t len, int prot)
{
	const DWORD flNewProtect = win32_prot(prot);
	DWORD flOldProtect;	// required by VirtualProtect
	BOOL ok = VirtualProtect(addr, len, flNewProtect, &flOldProtect);
	WARN_RETURN_IF_FALSE(ok);
	return 0;
}


// called when flags & MAP_ANONYMOUS
static LibError mmap_mem(void* start, size_t len, int prot, int flags, int fd, void** pp)
{
	// sanity checks. we don't care about these but enforce them to
	// ensure callers are compatible with mmap.
	// .. MAP_ANONYMOUS is documented to require this.
	debug_assert(fd == -1);
	// .. if MAP_SHARED, writes are to change "the underlying [mapped]
	//    object", but there is none here (we're backed by the page file).
	debug_assert(flags & MAP_PRIVATE);

	// see explanation at MAP_NORESERVE definition.
	bool want_commit = (prot != PROT_NONE && !(flags & MAP_NORESERVE));

	// decommit a given area (leaves its address space reserved)
	if(!want_commit && start != 0 && flags & MAP_FIXED)
	{
		MEMORY_BASIC_INFORMATION mbi;
		WARN_RETURN_IF_FALSE(VirtualQuery(start, &mbi, sizeof(mbi)));
		if(mbi.State == MEM_COMMIT)
		{
			WARN_IF_FALSE(VirtualFree(start, len, MEM_DECOMMIT));
			*pp = 0;
			// make sure *pp won't be misinterpreted as an error
			cassert(MAP_FAILED);
			return INFO::OK;
		}
	}

	DWORD flAllocationType = want_commit? MEM_COMMIT : MEM_RESERVE;
	DWORD flProtect = win32_prot(prot);
	void* p = VirtualAlloc(start, len, flAllocationType, flProtect);
	if(!p)
		WARN_RETURN(ERR::NO_MEM);
	*pp = p;
	return INFO::OK;
}


// given mmap prot and flags, output protection/access values for use with
// CreateFileMapping / MapViewOfFile. they only support read-only,
// read/write and copy-on-write, so we dumb it down to that and later
// set the correct (and more restrictive) permission via mprotect.
static LibError mmap_file_access(int prot, int flags, DWORD& flProtect, DWORD& dwAccess)
{
	// assume read-only; other cases handled below.
	flProtect = PAGE_READONLY;
	dwAccess  = FILE_MAP_READ;

	if(prot & PROT_WRITE)
	{
		// determine write behavior: (whether they change the underlying file)
		switch(flags & (MAP_SHARED|MAP_PRIVATE))
		{
			// .. changes are written to file.
		case MAP_SHARED:
			flProtect = PAGE_READWRITE;
			dwAccess  = FILE_MAP_WRITE;	// read and write
			break;
			// .. copy-on-write mapping; writes do not affect the file.
		case MAP_PRIVATE:
			flProtect = PAGE_WRITECOPY;
			dwAccess  = FILE_MAP_COPY;
			break;
			// .. either none or both of the flags are set. the latter is
			//    definitely illegal according to POSIX and some man pages
			//    say exactly one must be set, so abort.
		default:
			WARN_RETURN(ERR::INVALID_PARAM);
		}
	}

	return INFO::OK;
}


static LibError mmap_file(void* start, size_t len, int prot, int flags, int fd, off_t ofs, void** pp)
{
	debug_assert(fd != -1);	// handled by mmap_mem

	WIN_SAVE_LAST_ERROR;

	HANDLE hFile = HANDLE_from_intptr(_get_osfhandle(fd));
	if(hFile == INVALID_HANDLE_VALUE)
		WARN_RETURN(ERR::INVALID_PARAM);

	// MapViewOfFileEx will fail if the "suggested" base address is
	// nonzero but cannot be honored, so wipe out <start> unless MAP_FIXED.
	if(!(flags & MAP_FIXED))
		start = 0;

	// choose protection and access rights for CreateFileMapping /
	// MapViewOfFile. these are weaker than what PROT_* allows and
	// are augmented below by subsequently mprotect-ing.
	DWORD flProtect; DWORD dwAccess;
	RETURN_ERR(mmap_file_access(prot, flags, flProtect, dwAccess));

	// enough foreplay; now actually map.
	const HANDLE hMap = CreateFileMapping(hFile, 0, flProtect, 0, 0, (LPCSTR)0);
	// .. create failed; bail now to avoid overwriting the last error value.
	if(!hMap)
		WARN_RETURN(ERR::NO_MEM);
	const DWORD ofs_hi = u64_hi(ofs), ofs_lo = u64_lo(ofs);
	void* p = MapViewOfFileEx(hMap, dwAccess, ofs_hi, ofs_lo, (SIZE_T)len, start);
	// .. make sure we got the requested address if MAP_FIXED was passed.
	debug_assert(!(flags & MAP_FIXED) || (p == start));
	// .. free the mapping object now, so that we don't have to hold on to its
	//    handle until munmap(). it's not actually released yet due to the
	//    reference held by MapViewOfFileEx (if it succeeded).
	CloseHandle(hMap);
	// .. map failed; bail now to avoid "restoring" the last error value.
	if(!p)
		WARN_RETURN(ERR::NO_MEM);

	// slap on correct (more restrictive) permissions. 
	(void)mprotect(p, len, prot);

	WIN_RESTORE_LAST_ERROR;
	*pp = p;
	return INFO::OK;
}


void* mmap(void* start, size_t len, int prot, int flags, int fd, off_t ofs)
{
	void* p;
	LibError err;
	if(flags & MAP_ANONYMOUS)
		err = mmap_mem(start, len, prot, flags, fd, &p);
	else
		err = mmap_file(start, len, prot, flags, fd, ofs, &p);
	if(err < 0)
	{
		WARN_ERR(err);
		LibError_set_errno(err);
		return MAP_FAILED;
	}

	return p;
}


int munmap(void* start, size_t UNUSED(len))
{
	// UnmapViewOfFile checks if start was returned by MapViewOfFile*;
	// if not, it will fail.
	BOOL ok = UnmapViewOfFile(start);
	if(!ok)
		// VirtualFree requires dwSize to be 0 (entire region is released).
		ok = VirtualFree(start, 0, MEM_RELEASE);

	WARN_RETURN_IF_FALSE(ok);	// both failed
	return 0;
}

