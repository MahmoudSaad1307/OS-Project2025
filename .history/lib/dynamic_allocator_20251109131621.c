/*
 * dynamic_allocator.c
 *
 *  Created on: Sep 21, 2023
 *      Author: HP
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
//==================================
// [1] GET PAGE VA:
//==================================
_inline_ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	if (ptrPageInfo < &pageBlockInfoArr[0] || ptrPageInfo >= &pageBlockInfoArr[DYN_ALLOC_MAX_SIZE/PAGE_SIZE])
			panic("to_page_va called with invalid pageInfoPtr");
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================
// [2] GET PAGE INFO OF PAGE VA:
//==================================
_inline_ struct PageInfoElement * to_page_info(uint32 va)
{
	int idxInPageInfoArr = (va - dynAllocStart) >> PGSHIFT;
	if (idxInPageInfoArr < 0 || idxInPageInfoArr >= DYN_ALLOC_MAX_SIZE/PAGE_SIZE)
		panic("to_page_info called with invalid pa");
	return &pageBlockInfoArr[idxInPageInfoArr];
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator
	//Your code is here
	//Comment the following line
	//panic("initialize_dynamic_allocator() Not implemented yet");
	dynAllocStart=daStart;
	dynAllocEnd=daEnd;

	uint32 totalpages=(daEnd-daStart)/PAGE_SIZE;

	LIST_INIT(&freePagesList);



	  LIST_INSERT_HEAD(&freePagesList,(struct PageInfoElement *)&pageBlockInfoArr[0]);



	for(int i=1;i<totalpages;i++)
	{

		pageBlockInfoArr[i].block_size=0;
		pageBlockInfoArr[i].num_of_free_blocks=0;

 		LIST_INSERT_TAIL(&freePagesList,(struct PageInfoElement *)&pageBlockInfoArr[i] );

    }

			for(int i=0;i<LOG2_MAX_SIZE - LOG2_MIN_SIZE + 1;i++)
				{
				LIST_INIT(&freeBlockLists[i]);

				}


}

//===========================
// [2] GET BLOCK SIZE:
//===========================
_inline_ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size
	//Your code is here
	//Comment the following line
	//panic("get_block_size() Not implemented yet");

	uint32 pageindex = ((uint32)va-dynAllocStart)/PAGE_SIZE;

   return pageBlockInfoArr[pageindex].block_size;
}

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block
	//Your code is here
	//Comment the following line
	//panic("alloc_block() Not implemented yet");

	int index=0;
	int nearest=8;
	 while(nearest<size)
	      {
	    	  nearest*=2;
	    	  index++;

	      }
	 uint32 flag=100;
	 /*0 found ,
	  1 not found and freepage exist ,
	   2 not found and no freepage exist and larger free blocks exist ,
	   -1 not found and no freepage exist and no larger free blocks exist ,
     */
	 if (freeBlockLists[index].size!=0)
	     	 {
	     		 flag=0;

	     	 }
	 else if(freePagesList.size>0)
	 {
		 flag=1;

	 }
	 else
	 {
		 uint32 j=index;
		 while (j<LOG2_MAX_SIZE - LOG2_MIN_SIZE + 1)
		 {
			 if (freeBlockLists[j].size!=0)
				     	 {
	                		 flag=2;
	                        break;
				     	 }
			j++;
		 }
		 index=j;

		 if(flag!=2)
		 {
			 flag=-1;
		 }
	 }
      if(flag==0||flag==2)
      {
    	  struct BlockElement * element = LIST_FIRST(&freeBlockLists[index]);

    	  LIST_REMOVE(&freeBlockLists[index], element);

    	  uint32 pageindex = ((uint32)element-dynAllocStart)/PAGE_SIZE;

    	  pageBlockInfoArr[pageindex].num_of_free_blocks--;

    	  return element;
      }
      else if(flag==1)
      {


    	  struct PageInfoElement * freepage =LIST_FIRST(&freePagesList);
    	  uint16 blocks=PAGE_SIZE/nearest;
    	  freepage->block_size=nearest;
          freepage->num_of_free_blocks=blocks-1;
    	  LIST_REMOVE(&freePagesList,freepage);

    	  uint32 pageadd = to_page_va(freepage);
    	  get_page((void*)pageadd);

    	  LIST_INSERT_HEAD(&freeBlockLists[index],(struct BlockElement *)pageadd);


    	  for(int i=1;i< blocks;i++)
    	  {
     		LIST_INSERT_TAIL(&freeBlockLists[index],(struct BlockElement *)(((uint32)pageadd)+i*nearest) );
    	  }


    	  struct BlockElement * element2 = LIST_FIRST(&freeBlockLists[index]);

    	  LIST_REMOVE(&freeBlockLists[index], element2);




    	  return element2;

      }
      else {
    		panic("Sorry, can't allocat block");



      }


	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block
}

//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block
	//Your code is here
	//Comment the following line
	//panic("free_block() Not implemented yet");
	uint32 blocksize = get_block_size(va);

	int index=0;
		int nearest=8;
		 while(nearest<blocksize)
		      {
		    	  nearest*=2;
		    	  index++;

		      }

		 if(freeBlockLists[index].size==0)
		 {
			  LIST_INSERT_HEAD(&freeBlockLists[index],(struct BlockElement *)(va));

		 }
	 	 else{
	          LIST_INSERT_TAIL(&freeBlockLists[index],(struct BlockElement *)(va) );
		     }
	    uint32 pageindex = ((uint32)va-dynAllocStart)/PAGE_SIZE;

		pageBlockInfoArr[pageindex].num_of_free_blocks++;

		 if(pageBlockInfoArr[pageindex].num_of_free_blocks==PAGE_SIZE/pageBlockInfoArr[pageindex].block_size)
		 {

			 struct BlockElement *d;

			 LIST_FOREACH(d, &(freeBlockLists[index])) {
			  if( ((uint32)d-dynAllocStart)/PAGE_SIZE==pageindex)
								 {

							    	  LIST_REMOVE(&freeBlockLists[index], d);

								 }
			 }






			 uint32 pageadd =(pageindex*PAGE_SIZE+dynAllocStart);
			 return_page((void*)pageadd);

			 if(freePagesList.lh_first==NULL)
			 {
		    	  LIST_INSERT_HEAD(&freePagesList,(struct PageInfoElement *)&pageBlockInfoArr[pageindex]);

			 }
			 else{
		     		LIST_INSERT_TAIL(&freePagesList,(struct PageInfoElement *)&pageBlockInfoArr[pageindex]);

			 }
			 pageBlockInfoArr[pageindex].num_of_free_blocks=0;
			 pageBlockInfoArr[pageindex].block_size=0;

		 }

}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void realloc_block(void va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
	//Your code is here
	//Comment the following line
	panic("realloc_block() Not implemented yet");
}