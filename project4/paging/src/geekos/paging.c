/*
 * Paging (virtual memory) support
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.55 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/string.h>
#include <geekos/int.h>
#include <geekos/idt.h>
#include <geekos/kthread.h>
#include <geekos/kassert.h>
#include <geekos/screen.h>
#include <geekos/mem.h>
#include <geekos/malloc.h>
#include <geekos/gdt.h>
#include <geekos/segment.h>
#include <geekos/user.h>
#include <geekos/vfs.h>
#include <geekos/crc32.h>
#include <geekos/paging.h>

/* ----------------------------------------------------------------------
 * Public data
 * ---------------------------------------------------------------------- */

pde_t* kpde;
char* paging_file;
int pageout = 0;
int pagein = 0;

/* ----------------------------------------------------------------------
 * Private functions/data
 * ---------------------------------------------------------------------- */

#define SECTORS_PER_PAGE (PAGE_SIZE / SECTOR_SIZE)

/*
 * flag to indicate if debugging paging code
 */
int debugFaults = 0;
#define Debug(args...) if (debugFaults) Print(args)


void checkPaging()
{
  unsigned long reg=0;
  __asm__ __volatile__( "movl %%cr0, %0" : "=a" (reg));
  Print("Paging on ? : %d\n", (reg & (1<<31)) != 0);
}


/*
 * Print diagnostic information for a page fault.
 */
static void Print_Fault_Info(uint_t address, faultcode_t faultCode)
{
    extern uint_t g_freePageCount;
    Print("Pid %d, Page Fault received(%d pages free)\n",
        g_currentThread->pid, g_freePageCount);
    if (faultCode.protectionViolation)
        Print ("   Protection Violation, ");
    else
        Print ("   Non-present page, ");
    if (faultCode.writeFault)
        Print ("Write Fault, ");
    else
        Print ("Read Fault, ");
    if (faultCode.userModeFault)
        Print ("in User Mode\n");
    else
        Print ("in Supervisor Mode\n");
}

/*
 * Handler for page faults.
 * You should call the Install_Interrupt_Handler() function to
 * register this function as the handler for interrupt 14.
 */
/*static*/ void Page_Fault_Handler(struct Interrupt_State* state)
{
	ulong_t address;
    faultcode_t faultCode;

    KASSERT(!Interrupts_Enabled());
    
	/* Get the address that caused the page fault */
    address = Get_Page_Fault_Address();
    Debug("Page fault @%lx\n", address);

    /* Get the fault code */
    faultCode = *((faultcode_t *) &(state->errorCode));

    /* rest of your handling code here */
	Print ("Unexpected Page Fault received\n");
    Print_Fault_Info(address, faultCode);
    Dump_Interrupt_State(state);
    /* user faults just kill the process */
    if (!faultCode.userModeFault) KASSERT(0);
		
	pde_t* upde = g_currentThread->userContext->pageDir;
	pte_t* upte;
	void* memory;
	unsigned int table = (Round_Down_To_Page(address) << 10) >> 22;
	unsigned int directory = Round_Down_To_Page(address) >> 22;
	upte = (upde+directory)->pageTableBaseAddr << 12;

	// when stack overflow occured, handle page fault
	if(!faultCode.protectionViolation)// && faultCode.writeFault)
	{
		if((upte+table)->kernelInfo == KINFO_PAGE_ON_DISK)
		{
			memory = Alloc_Pageable_Page(upte+table, Round_Down_To_Page(address));
			memset(memory, 0, PAGE_SIZE);

			struct Page* page = Get_Page(memory);
			page->flags &= ~(PAGE_PAGEABLE);
			page->flags &= ~(PAGE_LOCKED);

			Enable_Interrupts();
			Read_From_Paging_File(memory, Round_Down_To_Page(address), (upte+table)->pageBaseAddr);
			Disable_Interrupts();
			
			page->flags |= PAGE_PAGEABLE;
			page->flags |= PAGE_LOCKED;

			(upte+table)->present = 1;
			(upte+table)->flags = VM_WRITE | VM_READ | VM_USER;
			(upte+table)->kernelInfo &= ~(KINFO_PAGE_ON_DISK);
			(upte+table)->pageBaseAddr = PAGE_ALLIGNED_ADDR(memory);
			return 0;
		}
		else
		{
			memory = Alloc_Pageable_Page(upte+table, Round_Down_To_Page(address));
			memset(memory, 0, PAGE_SIZE);

			(upte+table)->present = 1;
			(upte+table)->accesed = 1;
			(upte+table)->flags = VM_WRITE | VM_READ | VM_USER;
			(upte+table)->pageBaseAddr = PAGE_ALLIGNED_ADDR(memory);
			return 0;
		}
	}

    /* For now, just kill the thread/process. */
    Exit(-1);
}

/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */


/*
 * Initialize virtual memory by building page tables
 * for the kernel and physical memory.
 */
void Init_VM(struct Boot_Info *bootInfo)
{
	 int num_of_pages = bootInfo->memSizeKB >> 2;
	 //전체 메모리 크기(memSizeKB)를 4(2^2)로 나눔=페이지 개수
	 int num_of_dir_entries = Round_Up_To_Page(bootInfo->memSizeKB) / PAGE_SIZE;
	 int extraPages = num_of_pages % NUM_PAGE_TABLE_ENTRIES;
	//page table은 process마다 하나씩 존재하므로 extraPages는 페이지 테이블을 모두
	//채우지 못하는 경우의 남은 페이지의 개수가 된다
	 int i,j,tmpNumPages;
	 struct page* page;
	 


	 kpde = (pde_t*)Alloc_Page();	//하나의 페이지를 할당하여 pde_t구조체의 포인터를 리턴
	 memset(kpde,0,PAGE_SIZE);	//kpde로 시작하는 메모리 주소부터 페이지 크기만큼 0으로 채운다
	 pte_t* kpte;


	 for(i=0;i<NUM_PAGE_DIR_ENTRIES;i++)
	{
		//page directory 부분, i는 페이지 디렉토리의 인덱스이며 해당 index의 page table을 할당역할 
		//i가 page dir의 마지막 entry일 때 page table은 다 채우지 못하는 extraPages들을 할당하며
		//i가 다른 page dir의 entry인 경우는 1개의 page table의 entry개수 만큼 page 할당
	/*	if(i!=0)
		{
		kpte = (pte_t*)Alloc_Page();
		memset(kpte, 0, PAGE_SIZE);
		Enable_Paging(kpte);
		Install_Interrupt_Handler(PAGEFAULT_INT, &Page_Fault_Handler);
		}*/
		 for(j=0;j<NUM_PAGE_TABLE_ENTRIES ;j++ )
		 {//j는 페이지 테이블의 인덱스


			 if(i!=0 && j!=0)
			 {
				 kpte[j].present=1;
				 kpte[j].flags=VM_READ|VM_WRITE;//flags를 통해 해당page공간 속성변경
				 kpte[j].pageBaseAddr=j; 
				//j는 page table의 index이며 이것이 각각의 page table entry의 시작주소가 된다
			 }
		 }

	//해당 dir entry에서의 page table의 page를 모두 생성했으므로 다음과 같이 해당 dir entry의 필드값 변경
	(kpde+i)->present = 1;
	(kpde+i)->flags = VM_READ | VM_WRITE;
	(kpde+i)->accesed=0;
	(kpde+i)->reserved=0;
	(kpde+i)->largePages=0;
	(kpde+i)->globalPage=0;
	(kpde+i)->kernelInfo=0;


	(kpde+i)->pageTableBaseAddr=PAGE_ALLIGNED_ADDR(kpte);
	//page dir의 각각의 entry 시작주소는 
	//page(kpte)의 Address를 오른쪽으로 12칸 shift하여 2^12나눈다.
	}

	Enable_Paging(kpde);
	Install_Interrupt_Handler(PAGEFAULT_INT, &Page_Fault_Handler);
}

/**
 * Initialize paging file data structures.
 * All filesystems should be mounted before this function
 * is called, to ensure that the paging file is available.
 */
void Init_Paging(void)
{
	struct Paging_Device* paging_device = Get_Paging_Device();
	int size = paging_device->numSectors / 8;
	paging_file = (char*)Malloc(size);
	memset(paging_file, 0, size);
}

/**
 * Find a free bit of disk on the paging file for this page.
 * Interrupts must be disabled.
 * @return index of free page sized chunk of disk space in
 *   the paging file, or -1 if the paging file is full
 */
int Find_Space_On_Paging_File(void)
{
    KASSERT(!Interrupts_Enabled());
	struct Paging_Device* paging_device = Get_Paging_Device();
	int i, num_of_swap_page;

	num_of_swap_page = paging_device->numSectors / 8;

	for(i=0 ; i<num_of_swap_page ; i++)
	{
		if(paging_file[i] != 1)
		{
			paging_file[i] = 1;
			return i;
		}
	}

	return -1;
}

/**
 * Free a page-sized chunk of disk space in the paging file.
 * Interrupts must be disabled.
 * @param pagefileIndex index of the chunk of disk space
 */
void Free_Space_On_Paging_File(int pagefileIndex)
{
    KASSERT(!Interrupts_Enabled());
	paging_file[pagefileIndex] = 0;
}

/**
 * Write the contents of given page to the indicated block
 * of space in the paging file.
 * @param paddr a pointer to the physical memory of the page
 * @param vaddr virtual address where page is mapped in user memory
 * @param pagefileIndex the index of the page sized chunk of space
 *   in the paging file
 */
void Write_To_Paging_File(void *paddr, ulong_t vaddr, int pagefileIndex)
{
    struct Page *page = Get_Page((ulong_t) paddr);
    KASSERT(!(page->flags & PAGE_PAGEABLE)); /* Page must be locked! */
	struct Paging_Device* paging_device = Get_Paging_Device();
	int i;

	Print(">> written page file index : %d\n", pagefileIndex);

	for(i=0 ; i<8 ; i++)
	{
		Block_Write(paging_device->dev, paging_device->startSector + 8*pagefileIndex + i,  paddr + i*SECTOR_SIZE);
	}

	paging_file[pagefileIndex] = 1;
	pageout++;
}

/**
 * Read the contents of the indicated block
 * of space in the paging file into the given page.
 * @param paddr a pointer to the physical memory of the page
 * @param vaddr virtual address where page will be re-mapped in
 *   user memory
 * @param pagefileIndex the index of the page sized chunk of space
 *   in the paging file
 */
void Read_From_Paging_File(void *paddr, ulong_t vaddr, int pagefileIndex)
{
    struct Page *page = Get_Page((ulong_t) paddr);
    KASSERT(!(page->flags & PAGE_PAGEABLE)); /* Page must be locked! */
	struct Paging_Device* paging_device = Get_Paging_Device();
	int i;

	for(i=0 ; i<8 ; i++)
	{
		Block_Read(paging_device->dev, paging_device->startSector + 8*pagefileIndex + i,  paddr + i*SECTOR_SIZE);
	}

	paging_file[pagefileIndex] = 0;
	pagein++;
}
