/*
 * mm-implicit.c -  Simple allocator based on implicit free lists,
 *                  first fit placement, and boundary tag coalescing.
 *
 * Each block has header and footer of the form:
 *
 *      31                     3  2  1  0
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      -----------------------------------
 *
 * where s are the meaningful size bits and a/f is set
 * iff the block is allocated. The list has the following form:
 *
 * begin                                                          end
 * heap                                                           heap
 *  -----------------------------------------------------------------
 * |  pad   | hdr(8:a) | ftr(8:a) | zero or more usr blks | hdr(8:a) |
 *  -----------------------------------------------------------------
 *          |       prologue      |                       | epilogue |
 *          |         block       |                       | block    |
 *
 * The allocated prologue and epilogue blocks are overhead that
 * eliminate edge conditions during coalescing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
	/* Team name */
	"Akshay Joshi",
	/* First member's full name */
	"Akshay Joshi",
	/* First member's email address */
	"ajoshi6@mail.csuchico.edu",
	/* (leave blank) */
	"",
	/* (leave blank) */
	""
};

/////////////////////////////////////////////////////////////////////////////
// Constants and macros
//
// These correspond to the material in Figure 9.43 of the text
// The macros have been turned into C++ inline functions to
// make debugging code easier.
//
/////////////////////////////////////////////////////////////////////////////
#define WSIZE       4       /* word size (bytes) */
#define DSIZE       8       /* doubleword size (bytes) */
#define CHUNKSIZE  (1<<12)  /* initial heap size (bytes) */
#define OVERHEAD    8       /* overhead of header and footer (bytes) */



#define BUFFER (1<<7) //implemented by me

//added static inline MIN
static inline int MIN (int x, int y){
	return x < y ? x : y;
}
static inline int MAX(int x, int y) {
	return x > y ? x : y;
}

//
// Pack a size and allocated bit into a word
// We mask of the "alloc" field to insure only
// the lower bit is used
//
static inline size_t PACK(size_t size, int alloc) {
	return ((size) | (alloc & 0x1));
}

//
// Read and write a word at address p
//
static inline size_t GET(void *p) { return  *(size_t *)p; }
static inline void PUT( void *p, size_t val)
{
	*((size_t *)p) = val;
}

//Put Clear function
static inline void PUT_CLEAR(void *p,size_t val)
{
	*(unsigned int *)(p) = (val);
}
//
// Read the size and allocated fields from address p
//
static inline size_t GET_SIZE( void *p )  {
	return GET(p) & ~0x7;
}

static inline int GET_ALLOC( void *p  ) {
	return GET(p) & 0x1;
}


//Get the tag and set it unset the bit after use;
static inline int GET_TAG(void *p){
	return GET(p) & 0x2;
}
static inline void SET_TAG(void *p){
	(*(unsigned int *)(p)=GET(p) | 0x2);
}
static inline void UNSET_TAG(void *p){
	(*(unsigned int *)(p)=GET(p) & ~0x2);
}
//
// Given block ptr bp, compute address of its header and footer
//
static inline void *HDRP(void *bp) {
	return ( (char *)bp) - WSIZE;
}
static inline void *FTRP(void *bp) {
	return ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE);
}

//
// Given block ptr bp, compute address of next and previous blocks
//
static inline void *NEXT_BLKP(void *bp) {
	return  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)));
}

static inline void* PREV_BLKP(void *bp){
	return  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)));
}



/* Address of free block's predecessor and successor entries */
static inline void* PRED_PTR(void *bp){
	return ((char *)(bp));
}
static inline void* SUCC_PTR(void* bp){
	return ((char *)(bp) + WSIZE);
}
/*The address of the blocks before and after pointer on a segregated list*/
static inline void* PRED(void *bp){
	return (*(char **)(bp));
}
static inline void* SUCC(void *bp){
	return (*(char **)(SUCC_PTR(bp)));
}
/* Store predecessor or successor pointer for free blocks */
static inline void SET_PTR(void* p,void* bp){
	(*(unsigned int *)(p) = (unsigned int)(bp));
}


/////////////////////////////////////////////////////////////////////////////
//
// Global Variables
//

static char *heap_listp;  /* pointer to first block */
void *free_list[20];
//
// function prototypes for internal helper routines
//
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp);
static void checkblock(void *bp);


static void delete(void *bp);
static void insert(void *bp, size_t size);


//
// mm_init - Initialize the memory manager
//
int mm_init(void)
{
	int ListCounter;		//This counter is for list
	char* StartPtr;	//Starting Ptr of heap

	for(ListCounter=0;ListCounter<20;ListCounter++)
	{
		free_list[ListCounter]=NULL;
	}

	if ((StartPtr = mem_sbrk(4*WSIZE)) == (void *)-1) //Expands the heap by incr bytes
	{
		return -1;
	}

	PUT_CLEAR(StartPtr, 0);
	PUT_CLEAR(StartPtr + (1*WSIZE), PACK(DSIZE, 1)); // putting header
	PUT_CLEAR(StartPtr + (2*WSIZE), PACK(DSIZE, 1)); // putting footer
	PUT_CLEAR(StartPtr + (3*WSIZE), PACK(0, 1));
	heap_listp = StartPtr + DSIZE;

	if (extend_heap(CHUNKSIZE/WSIZE) == NULL)//extending the heap
	{
		return -1;
	}

	return 0;
}


//
// extend_heap - Extend heap with free block and return its block pointer
//
static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;

	size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; //even number of words arranged here
	if ((long)(bp = mem_sbrk(size)) == -1)
	{
		return NULL;
	}

	PUT_CLEAR(HDRP(bp), PACK(size, 0));
	PUT_CLEAR(FTRP(bp), PACK(size, 0));
	PUT_CLEAR(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
	//header footer for free blocks are Initialized
	insert(bp, size);
	//inserting it in free list
	return coalesce(bp); //Coalesce if preovious block was free
}


//
// Practice problem 9.8
//
// find_fit - Find a fit for a block with asize bytes
//
static void *find_fit(size_t asize)
{
	return NULL; /* no fit */
}

//
// mm_free - Free a block
//
void mm_free(void *bp)
{
	if(bp == 0)
	{
		return;
	}

	size_t size = GET_SIZE(HDRP(bp));
	UNSET_TAG(HDRP(NEXT_BLKP(bp)));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	insert(bp, size);
	coalesce(bp); //again insert to free list and coalesce
}

//
// coalesce - boundary tag coalescing. Return ptr to coalesced block
//
static void *coalesce(void *bp)
{
	return bp;
}

//
// mm_malloc - Allocate a block with at least size bytes of payload
//


void *mm_malloc(size_t size)
{
	size_t asize;      //adjusted block size
	size_t extendsize; // extend heap limit
	char *bp;
	int ListCounter=0;  //list counter

	if (heap_listp == 0)
	{
		mm_init();
	}
	if (size == 0)
	{
		return NULL;
	}
  if(size > DSIZE)//block size adjustment
	{
		asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
	}
	else
	{
		asize = 2*DSIZE;
	}
	find_fit(asize);//finding free block in the list
	size = asize;
	for(ListCounter=0;ListCounter<20;++ListCounter)
	{
		if((ListCounter==19) ||((size<=1)&&(free_list[ListCounter]!=NULL)))
		{
			bp = free_list[ListCounter];
			while((bp != NULL)&&((asize > GET_SIZE(HDRP(bp))) || (GET_TAG(HDRP(bp)))))
			{
				bp = PRED(bp);
			}
			if(bp == NULL)
			{
        continue;
			}
      else if(bp != NULL)
      {
        break;
      }
		}
		size>>=1;
	}

	// No fitting free block found
	if(bp==NULL)
	{
		extendsize = MAX(asize,CHUNKSIZE);//extend it by the chunksize
		if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
		{
			return NULL;
		}
	}
	place(bp, asize);
	return bp;
}

//
//
// Practice problem 9.9
//
// place - Place block of asize bytes at start of free block bp
//         and split if remainder would be at least minimum block size
//
static void place(void *bp, size_t asize)
{
	size_t csize = GET_SIZE(HDRP(bp));
	delete(bp);  //removing block

	if((csize - asize) < (2*DSIZE))
	{
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
	else
	{
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		PUT_CLEAR(HDRP(NEXT_BLKP(bp)), PACK(csize-asize, 0));
		PUT_CLEAR(FTRP(NEXT_BLKP(bp)), PACK(csize-asize, 0));
		insert(NEXT_BLKP(bp), (csize-asize));
	}

}


//
// mm_realloc -- implemented for you
//new implementation
void *mm_realloc(void *bp, size_t size)
{
	void *new_bp = bp; // Pointer to be returned
	int remainder; // to check addequet blocks
	int extendsize; //size of the extended heap
	int block_buffer; // buffer block size
	if (size == 0)
  {
  		return NULL;
  }//block size adjustment

  if(size > DSIZE)
  {
		size = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
	}
  else
  {
		size = 2 * DSIZE;
	}	// Adding overhead here
	size += BUFFER;
	block_buffer = GET_SIZE(HDRP(bp)) - size;//calculating block buffer
	if (block_buffer < 0) //allocating more space
  {
		if (!GET_ALLOC(HDRP(NEXT_BLKP(bp))) || !GET_SIZE(HDRP(NEXT_BLKP(bp))))
    {     //check if next block is free or not
			remainder = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp))) - size;
			if (remainder < 0)
      {
				extendsize = MAX(-remainder, CHUNKSIZE);
				if (extend_heap(extendsize/WSIZE) == NULL)
        {
					return NULL;
        }
				remainder += extendsize;
			}
			delete(NEXT_BLKP(bp));
			PUT_CLEAR(HDRP(bp), PACK(size + remainder, 1)); //header
			PUT_CLEAR(FTRP(bp), PACK(size + remainder, 1)); // footer
		}
		else
		{
			new_bp = mm_malloc(size - DSIZE);
			memmove(new_bp, bp, MIN(size, size - BUFFER));
			mm_free(bp);
		}
		block_buffer = GET_SIZE(HDRP(new_bp)) - size;
	}
	if (block_buffer < 2 * BUFFER) //next block if over head tagging more times
		{
			SET_TAG(HDRP(NEXT_BLKP(new_bp)));
		}
	return new_bp;//Returning realloc block
}

//
// mm_checkheap - Check the heap for consistency
//
void mm_checkheap(int verbose)
{
	//
	// This provided implementation assumes you're using the structure
	// of the sample solution in the text. If not, omit this code
	// and provide your own mm_checkheap
	//
	void *bp = heap_listp;

	if (verbose) {
		printf("Heap (%p):\n", heap_listp);
	}

	if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp))) {
		printf("Bad prologue header\n");
	}
	checkblock(heap_listp);

	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (verbose)  {
			printblock(bp);
		}
		checkblock(bp);
	}

	if (verbose) {
		printblock(bp);
	}

	if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp)))) {
		printf("Bad epilogue header\n");
	}
}

static void printblock(void *bp)
{
	size_t hsize, halloc, fsize, falloc;

	hsize = GET_SIZE(HDRP(bp));
	halloc = GET_ALLOC(HDRP(bp));
	fsize = GET_SIZE(FTRP(bp));
	falloc = GET_ALLOC(FTRP(bp));

	if (hsize == 0) {
		printf("%p: EOL\n", bp);
		return;
	}

	printf("%p: header: [%d:%c] footer: [%d:%c]\n",
			bp,
			(int) hsize, (halloc ? 'a' : 'f'),
			(int) fsize, (falloc ? 'a' : 'f'));
}

static void checkblock(void *bp)
{
	if ((size_t)bp % 8) {
		printf("Error: %p is not doubleword aligned\n", bp);
	}
	if (GET(HDRP(bp)) != GET(FTRP(bp))) {
		printf("Error: header does not match footer\n");
	}
}















static void insert(void *bp, size_t size)
{
	void *search_bp = bp;
	void *insert_bp = NULL;
	int ListCounter = 0;

	while ((ListCounter < 19) && (size > 1))
	{
		size >>= 1;
		ListCounter++;
	}

	/* Select location on ListCounter to insert pointer while keeping ListCounter
	   organized by byte size in ascending order. */
	search_bp = free_list[ListCounter];
	while ((search_bp != NULL) && (size > GET_SIZE(HDRP(search_bp)))) {
		insert_bp = search_bp;
		search_bp = PRED(search_bp);
	}


	/* Set predecessor and successor */
	if (search_bp != NULL) {
		if (insert_bp != NULL) {
			SET_PTR(PRED_PTR(bp), search_bp);
			SET_PTR(SUCC_PTR(search_bp), bp);
			SET_PTR(SUCC_PTR(bp), insert_bp);
			SET_PTR(PRED_PTR(insert_bp), bp);
		} else {
			SET_PTR(PRED_PTR(bp), search_bp);
			SET_PTR(SUCC_PTR(search_bp), bp);
			SET_PTR(SUCC_PTR(bp), NULL);
			/* Add block to list */
			free_list[ListCounter] = bp;
		}
	} else {
		if (insert_bp != NULL) {
			SET_PTR(PRED_PTR(bp), NULL);
			SET_PTR(SUCC_PTR(bp), insert_bp);
			SET_PTR(PRED_PTR(insert_bp), bp);
		} else {
			SET_PTR(PRED_PTR(bp), NULL);
			SET_PTR(SUCC_PTR(bp), NULL);
			/* Add block to list */
			free_list[ListCounter] = bp;
		}
	}
}














static void delete(void *bp) {
	int ListCounter = 0;
	size_t size = GET_SIZE(HDRP(bp));
	/* Select list */
	while ((ListCounter < 19) && (size > 1)) {
		size >>= 1;
		ListCounter++;
	}
	if (PRED(bp) != NULL) {
		if (SUCC(bp) != NULL) {
			SET_PTR(SUCC_PTR(PRED(bp)), SUCC(bp));
			SET_PTR(PRED_PTR(SUCC(bp)), PRED(bp));
		} else {
			SET_PTR(SUCC_PTR(PRED(bp)), NULL);
			free_list[ListCounter] = PRED(bp);
		}
	} else {
		if (SUCC(bp) != NULL) {
			SET_PTR(PRED_PTR(SUCC(bp)), NULL);
		} else {
			free_list[ListCounter] = NULL;
		}
	}
	return;
}
