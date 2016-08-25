/*
 * ELF executable loading
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003, David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.29 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/errno.h>
#include <geekos/kassert.h>
#include <geekos/ktypes.h>
#include <geekos/screen.h>  /* for debug Print() statements */
#include <geekos/pfat.h>
#include <geekos/malloc.h>
#include <geekos/string.h>
#include <geekos/user.h>
#include <geekos/elf.h>


/**
 * From the data of an ELF executable, determine how its segments
 * need to be loaded into memory.
 * @param exeFileData buffer containing the executable file
 * @param exeFileLength length of the executable file in bytes
 * @param exeFormat structure describing the executable's segments
 *   and entry address; to be filled in
 * @return 0 if successful, < 0 on error
 */
int Parse_ELF_Executable(char *exeFileData, ulong_t exeFileLength,
    struct Exe_Format *exeFormat)
{
	elfHeader* ehdr;
	programHeader* phdr;
	int i;

	ehdr = (elfHeader*)exeFileData;

	if(exeFileData == 0)
	{
		Print("exeFileData == 0");
		return -1;
	}

	if(ehdr->ident[0] != 0x7F || ehdr->ident[1] != 'E'
			|| ehdr->ident[2] != 'L' || ehdr->ident[3] != 'F')
	{
		Print("ehdr->ident is not ELF");
		return -1;
	}

	if(ehdr->phnum <= EXE_MAX_SEGMENTS)
	{
		exeFormat->numSegments = ehdr->phnum;
		exeFormat->entryAddr = ehdr->entry;

		for(i=0 ; i<ehdr->phnum ; i++)
		{
			phdr = (programHeader*)(exeFileData + ehdr->phoff + i * ehdr->phentsize);

			exeFormat->segmentList[i].offsetInFile = phdr->offset;
			exeFormat->segmentList[i].lengthInFile = phdr->fileSize;
			exeFormat->segmentList[i].startAddress = phdr->vaddr;
			exeFormat->segmentList[i].sizeInMemory = phdr->memSize;
			exeFormat->segmentList[i].protFlags = phdr->flags;
		}
	}

	return 0;
}
