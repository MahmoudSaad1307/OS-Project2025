/*
 * fault_handler.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>

//2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
// 0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
//2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
/*2024*/ void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
/*2025*/ void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };

//2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

//==================
// [0] INIT HANDLER:
//==================
void fault_handler_init()
{
	//setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
	//setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	//setPageReplacmentAlgorithmModifiedCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0) ;
	setModifiedBufferLength(1000);
}

//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;
extern uint32 sys_calculate_free_frames() ;

struct Env* last_faulted_env = NULL;
void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	// Read processor's CR2 register to find the faulting address
	uint32 fault_va = rcr2();
	//cprintf("************Faulted VA = %x************\n", fault_va);
	//	print_trapframe(tf);
	/******************************************************/

	//If same fault va for 3 times, then panic
	//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
	struct Env* cur_env = get_cpu_proc();
	if (last_fault_va == fault_va && last_faulted_env == cur_env)
	{
		num_repeated_fault++ ;
		if (num_repeated_fault == 3)
		{
			print_trapframe(tf);
			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
		}
	}
	else
	{
		before_last_fault_va = last_fault_va;
		before_last_eip = last_eip;
		num_repeated_fault = 0;
	}
	last_eip = (uint32)tf->tf_eip;
	last_fault_va = fault_va ;
	last_faulted_env = cur_env;
	/******************************************************/
	//2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3) {
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu* c = mycpu();
		//cprintf("trap from KERNEL\n");
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	//2017: Check stack underflow for User
	else
	{
		//cprintf("trap from USER\n");
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	//get a pointer to the environment that caused the fault at runtime
	//cprintf("curenv = %x\n", curenv);
	struct Env* faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		cprintf("\nFaulted VA = %x\n", fault_va);
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}
	//check the faulted address, is it a table or not ?
	//If the directory entry of the faulted address is NOT PRESENT then
	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		faulted_env->tableFaultsCounter ++ ;
		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{
			/*============================================================================================*/
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #2 Check for invalid pointers
			//(e.g. pointing to unmarked user heap page, kernel or wrong access rights),
			
			// Check if trying to access kernel space
			if (fault_va >= KERNEL_BASE) 
			{
				cprintf("[%08x] user fault va %08x ip %08x: trying to access kernel\n",
						faulted_env->env_id, fault_va, tf->tf_eip);
				env_exit();
			}
			
			// Check if in heap range
			if (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) 
			{
				uint32* ptr_page_table = NULL;
				get_page_table(faulted_env->env_page_directory, fault_va, &ptr_page_table);
				
				if (ptr_page_table != NULL) 
				{
					uint32 pte = ptr_page_table[PTX(fault_va)];
					
					// Check if not present and not marked as heap page
					if ((pte & PERM_PRESENT) == 0) 
					{
						if ((pte & PERM_UHPAGE) == 0) 
						{
							cprintf("[%08x] user fault va %08x ip %08x: unmarked heap page\n",
									faulted_env->env_id, fault_va, tf->tf_eip);
							env_exit();
						}
					}
				}
			}
			
			// Check for write to read-only page
			uint32* ptr_page_table = NULL;
			get_page_table(faulted_env->env_page_directory, fault_va, &ptr_page_table);
			
			if (ptr_page_table != NULL) 
			{
				uint32 pte = ptr_page_table[PTX(fault_va)];
				
				// If page exists but not writable
				if (pte & PERM_PRESENT) 
				{
					if (!(pte & PERM_WRITEABLE)) 
					{
						// Check if trying to write
						if (tf->tf_err & FEC_WR) 
						{
							cprintf("[%08x] user fault va %08x ip %08x: write to read-only\n",
									faulted_env->env_id, fault_va, tf->tf_eip);
							env_exit();
						}
					}
				}
			}

			/*============================================================================================*/
		}

		/*2022: Check if fault due to Access Rights */
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;
		/*============================================================================================*/


		// we have normal page fault =============================================================
		faulted_env->pageFaultsCounter ++ ;

//				cprintf("[%08s] user PAGE fault va %08x\n", faulted_env->prog_name, fault_va);
//				cprintf("\nPage working set BEFORE fault handler...\n");
//				env_page_ws_print(faulted_env);
		//int ffb = sys_calculate_free_frames();

		if(isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			page_fault_handler(faulted_env, fault_va);
		}

		//		cprintf("\nPage working set AFTER fault handler...\n");
		//		env_page_ws_print(faulted_env);
		//		int ffa = sys_calculate_free_frames();
		//		cprintf("fault handling @%x: difference in free frames (after - before = %d)\n", fault_va, ffa - ffb);
	}

	/*************************************************************/
	//Refresh the TLB cache
	tlbflush();
	/*************************************************************/
}


//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	//panic("table_fault_handler() is not implemented yet...!!");
	//Check if it's a stack page
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//===============================
// [2.5] CREATE KERNEL STACK:
//===============================
void* create_user_kern_stack(uint32* ptr_user_page_directory)
{
	//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #1 create_user_kern_stack
	
	// Allocate kernel stack memory
	void* stack = kmalloc(KERNEL_STACK_SIZE);
	
	if (stack == NULL) 
	{
		panic("create_user_kern_stack: kmalloc failed");
	}
	
	// Get the bottom page address (guard page)
	uint32 guard_va = (uint32)stack;
	
	// Get the page table for this virtual address
	uint32* page_table = NULL;
	get_page_table(ptr_user_page_directory, guard_va, &page_table);
	
	if (page_table != NULL) 
	{
		// Remove present bit to mark as guard page
		page_table[PTX(guard_va)] = page_table[PTX(guard_va)] & (~PERM_PRESENT);
		
		// Clear TLB entry
		tlb_invalidate(ptr_user_page_directory, (void*)guard_va);
	}
	
	return stack;
}

//===============================
// HELPER: env_page_ws_invalidate
//===============================
void env_page_ws_invalidate(struct Env* e, uint32 virtual_address)
{
#if USE_KHEAP
	// Search in the working set list
	struct WorkingSetElement *element = NULL;
	struct WorkingSetElement *current = NULL;
	
	LIST_FOREACH(current, &(e->page_WS_list))
	{
		if (current->virtual_address == virtual_address)
		{
			element = current;
			break;
		}
	}
	
	if (element != NULL)
	{
		// Update clock hand if needed
		if (e->page_last_WS_element == element)
		{
			e->page_last_WS_element = LIST_NEXT(element);
			if (e->page_last_WS_element == NULL)
			{
				e->page_last_WS_element = LIST_FIRST(&(e->page_WS_list));
			}
		}
		
		// Remove from list
		LIST_REMOVE(&(e->page_WS_list), element);
		
		// Free memory
		kfree(element);
		
		// Unmap the page
		unmap_frame(e->env_page_directory, virtual_address);
	}
#else
	// Search in working set array
	uint32 wsSize = env_page_ws_get_size(e);
	int found = -1;
	
	for (int i = 0; i < wsSize; i++)
	{
		if (e->ptr_pageWorkingSet[i].virtual_address == virtual_address)
		{
			found = i;
			break;
		}
	}
	
	if (found != -1)
	{
		// Shift all elements after the found one
		for (int i = found; i < wsSize - 1; i++)
		{
			e->ptr_pageWorkingSet[i] = e->ptr_pageWorkingSet[i + 1];
		}
		
		// Clear last element
		e->ptr_pageWorkingSet[wsSize - 1].virtual_address = 0;
		e->ptr_pageWorkingSet[wsSize - 1].time_stamp = 0;
		e->ptr_pageWorkingSet[wsSize - 1].sweeps_counter = 0;
		
		// Adjust clock hand
		if (e->page_last_WS_index >= wsSize - 1)
		{
			e->page_last_WS_index = 0;
		}
		
		// Unmap the page
		unmap_frame(e->env_page_directory, virtual_address);
	}
#endif
}

//===============================
// HELPER: env_page_ws_print
//===============================
void env_page_ws_print(struct Env* e)
{
	cprintf("\n===== Working Set for Process [%s] =====\n", e->prog_name);
	cprintf("Process ID: %d\n", e->env_id);
	cprintf("Max WS Size: %d\n", e->page_WS_max_size);
	
#if USE_KHEAP
	uint32 size = LIST_SIZE(&(e->page_WS_list));
	cprintf("Current WS Size: %d\n\n", size);
	
	if (size == 0)
	{
		cprintf("Working Set is EMPTY\n");
		cprintf("=========================================\n\n");
		return;
	}
	
	cprintf("Index  Virtual_Addr  Used  Modified  Present\n");
	cprintf("-----  ------------  ----  --------  -------\n");
	
	struct WorkingSetElement *element = NULL;
	int index = 0;
	
	LIST_FOREACH(element, &(e->page_WS_list))
	{
		uint32 va = element->virtual_address;
		uint32 perms = pt_get_page_permissions(e->env_page_directory, va);
		
		char marker = ' ';
		if (element == e->page_last_WS_element)
		{
			marker = '*';
		}
		
		cprintf("%c%-4d  0x%08x    %-4s  %-8s  %-7s\n", 
		        marker,
		        index,
		        va,
		        (perms & PERM_USED) ? "YES" : "NO",
		        (perms & PERM_MODIFIED) ? "YES" : "NO",
		        (perms & PERM_PRESENT) ? "YES" : "NO");
		
		index++;
	}
	
	cprintf("\n* = Clock Hand Position\n");
	
#else
	uint32 size = env_page_ws_get_size(e);
	cprintf("Current WS Size: %d\n\n", size);
	
	if (size == 0)
	{
		cprintf("Working Set is EMPTY\n");
		cprintf("=========================================\n\n");
		return;
	}
	
	cprintf("Index  Virtual_Addr  Used  Modified  Present\n");
	cprintf("-----  ------------  ----  --------  -------\n");
	
	for (int i = 0; i < size; i++)
	{
		uint32 va = e->ptr_pageWorkingSet[i].virtual_address;
		uint32 perms = pt_get_page_permissions(e->env_page_directory, va);
		
		char marker = ' ';
		if (i == e->page_last_WS_index)
		{
			marker = '*';
		}
		
		cprintf("%c%-4d  0x%08x    %-4s  %-8s  %-7s\n", 
		        marker,
		        i,
		        va,
		        (perms & PERM_USED) ? "YES" : "NO",
		        (perms & PERM_MODIFIED) ? "YES" : "NO",
		        (perms & PERM_PRESENT) ? "YES" : "NO");
	}
	
	cprintf("\n* = Clock Hand Position\n");
#endif
	
	cprintf("=========================================\n\n");
}

//===============================
// HELPER: env_page_ws_list_create_element
//===============================
struct WorkingSetElement* env_page_ws_list_create_element(struct Env* e, uint32 virtual_address)
{
#if USE_KHEAP
	// Allocate memory for new element
	struct WorkingSetElement* element = (struct WorkingSetElement*)kmalloc(sizeof(struct WorkingSetElement));
	
	if (element == NULL)
	{
		panic("env_page_ws_list_create_element: kmalloc failed");
	}
	
	// Initialize the element fields
	element->virtual_address = virtual_address;
	element->time_stamp = 0;
	element->sweeps_counter = 0;
	element->prev_next_info.le_next = NULL;
	element->prev_next_info.le_prev = NULL;
	
	return element;
#else
	panic("env_page_ws_list_create_element: called with array mode");
	return NULL;
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
/* Calculate the number of page faults according th the OPTIMAL replacement strategy
 * Given:
 * 	1. Initial Working Set List (that the process started with)
 * 	2. Max Working Set Size
 * 	3. Page References List (contains the stream of referenced VAs till the process finished)
 *
 * 	IMPORTANT: This function SHOULD NOT change any of the given lists
 */
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
	//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
	//Your code is here
	//Comment the following line
	panic("get_optimal_num_faults() is not implemented yet...!!");
}

void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{
#if USE_KHEAP
	struct WorkingSetElement *victimWSElement = NULL;
	uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
#else
	int iWS =faulted_env->page_last_WS_index;
	uint32 wsSize = env_page_ws_get_size(faulted_env);
#endif
	if(wsSize < (faulted_env->page_WS_max_size))
	{
		//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
		//Your code is here
		//Comment the following line
		panic("page_fault_handler().PLACEMENT is not implemented yet...!!");
	}
	else
	{
		if (isPageReplacmentAlgorithmOPTIMAL())
		{
			//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
			//Your code is here
			//Comment the following line
			panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
		}
		else if (isPageReplacmentAlgorithmOPTIMAL())
		{
			//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
			//Your code is here
			//Comment the following line
			panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
		}
		else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
		{
			//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
			//Your code is here
			//Comment the following line
			panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
		}
		else if (isPageReplacmentAlgorithmModifiedCLOCK())
		{
			//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
			//Your code is here
			//Comment the following line
			panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
		}
	}
}
void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}