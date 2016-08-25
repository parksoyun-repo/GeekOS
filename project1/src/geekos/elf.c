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
	//#1 elfHeader구조체의 pointer 지정
	elfHeader *p_elfHeader = (elfHeader *)exeFileData;

	//#2 programHeader 구조체의 pointer 지정
	programHeader *p_programHeader = (programHeader *)(exeFileData + p_elfHeader->phoff);
	
	//#3 예외처리 시작

	//매직넘버 검사
	if(p_elfHeader->ident[0] != 0x7f ||
	   p_elfHeader->ident[1] != 'E'  ||
	   p_elfHeader->ident[2] != 'L'  ||
	   p_elfHeader->ident[3] != 'F')
	{
		Print("Wrong magic number\n");
		return -1;
	}

	//ELF header의 버전 검사
	if(p_elfHeader->ident[6] != 1)
	{
		Print("Invalide ELF version\n");
		return -1;
	}
	
	//ELF 파일의 type검사 : unknown type이면 종료
	if(p_elfHeader->type == 0)
	{
		Print("Unknown type of file\n");
		return -1;
	}
	
	//#4 elfHeader와 programHeader의 정보 파싱 시작
	exeFormat->numSegments = p_elfHeader->phnum;
	exeFormat->entryAddr = p_elfHeader->entry;

	int i = 0;
	for(i = 0; i < p_elfHeader->phnum; i++)
	{
		exeFormat->segmentList[i].offsetInFile = p_programHeader[i].offset;
		exeFormat->segmentList[i].lengthInFile = p_programHeader[i].fileSize;
		exeFormat->segmentList[i].startAddress = p_programHeader[i].vaddr;
		exeFormat->segmentList[i].sizeInMemory = p_programHeader[i].memSize;
		exeFormat->segmentList[i].protFlags = p_programHeader[i].flags;
	}
	return 0;
}
