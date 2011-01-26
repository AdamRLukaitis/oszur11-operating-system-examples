/*! Dynamic memory allocator - Grid Memory Allocator (GMA), based on TLSF */

#pragma once

/*! interface to kernel and other code (not for gma.c) */
#ifndef _GMA_C_

#define gma_t	void

gma_t *gma_init ( void *memory_segment, size_t size, size_t min_chunk_size,
		    uint flags );
void *gma_alloc ( gma_t *mpool, size_t size );
int gma_free ( gma_t *mpool, void *address );

/*
  Memory allocator is named Grid Memory Allocator (GMA) because of two level
  list organization (like grid). GMA implements TLSF algorithm.

  TLSF - short explanation

  (look at authors site: http://rtportal.upv.es/rtmalloc/ for more details )

  TLSF - Two Level Segregate First is an Good-Fit memory allocator that uses
  two dimensional array of lists for free chunks: chunk[fl][sl] (each element
  is an list). Sizes of chunks in particular list depends on indexes:
	fl (first level) and sl (second level).

  min_size{chunk[fl][sl]} = 2^fl + sl * step(fl)

  Since:  min_size (fl, 0) = 2^fl  and  min_size (fl+1, 0) = 2^(fl+1)
	  and range of second level index 'sl' is: 0 to 2^L-1
	  (L is defined by processor word size: L = msb_index (__WORD_SIZE) )
	  ('msb_index' returns index of most significant bit that is set)
  then:
  step(fl) = (2^(fl+1) - 2^fl)/2^L = 2^(fl-L)

  Size of chunk[fl][sl] is from interval:
     2^fl + sl * 2^(fl-L) <= sizeof{chunk[fl][sl]} < 2^fl + (sl+1) * 2^(fl-L)

  When inserting free chunk in list, fl and sl are calculated as:
	size = 2^i + remainder
	fl = msb_index(size);
	sl = remainder / step(fl) = (size - 2^fl) / 2^(fl-L)
	   = size / 2^(fl-L) - 2^L
	:::
	fl = msb_index(size);
	sl = (size >> (fl-L)) - (1 << L)

  When searching for free chunk of 'size' we can't use same formulas! In list
  chunk[fl][sl] might be chunk of requested size or may not be. If it is, it
  might be after many smaller ones. For search to be in O(1) we look in list
  that surely have chunk of requested size. That's first next list (exception is
  when requested size is equal to minimum size of chunks that list holds).
  To use same formulas we first increase requested size by step(x)-1:
	fl1 = msb_index(size)
	size = size + (step(fl1) - 1) = size + (2^(fl1 - L) - 1) =
	     = size + (2^(msb_index(size) - L) - 1)

   and then use same formula, only on adjusted 'size':
	fl = msb_index(size);
	sl = (size >> (fl-L)) - (1 << L)

   However, calculated list might be empty! Using bitmaps we can find first next
   list with larger free chunks in O(1).
   Bitmaps:
	FL_bitmap - bitmap for first level index (in which rows there are free
		    chunks, which SL_bitmaps[x] are not zero)
	SL_bitmap[] - bitmaps for second level index: each bit of SL_bitmap[i]
		      represent one list in chunk[i][j], when j-th bit is set,
		      chunk[i][j] is not empty - has at least one free chunk
*/
#else /* _GMA_C_ */

/*! rest is only for gma.c */

#include <lib/types.h>

/* 'L' is defined with processor's word size (tested only on 32bit machine!) */
#if	__WORD_SIZE == 8
#define	L 3
#elif	__WORD_SIZE == 16
#define L 4
#elif	__WORD_SIZE == 32
#define L 5
#elif	__WORD_SIZE == 64
#define L 6
#elif	__WORD_SIZE == 128
#define L 7
#else
#error Undefined or unsupported required constant __WORD_SIZE
#endif

/* Second level dimension */
#define SL_DIM	__WORD_SIZE	/* ( 1 << L ) */

struct _mchunk_t_; /* memory chunks, defined later */
typedef struct _mchunk_t_ mchunk_t;

/*! Memory pool data */
typedef struct _gma_t_
{
	unsigned int fl_min, fl_max;
	/* minimal and maximal first level indexes that are used */

	size_t min_chunk_size;	/* do not split chunks into smaller pieces */

	size_t FL_bitmap;	/* bitmap for first level */
	size_t *SL_bitmap;	/* bitmaps for second levels */

	mchunk_t *(*chunk)[SL_DIM]; /* 2-level array list headers  */
}				    /* chunk[i][j] is of type (mchunk_t *) */
gma_t;


/*! Memory chunk organization is similar to DL malloc (Doug Lea) */
struct _mchunk_t_
{
	/* Size of chunk before this (if free) -- NOT PART OF THIS CHUNK!!! */
	size_t bsize;

	/* Size and "in use" bits -- STARTING PART OF THIS CHUNK */
	size_t size;

	/* double linked list -- USED ONLY IF CHUNK IS FREE */
	struct _mchunk_t_ *prev;
	struct _mchunk_t_ *next;
};

/*
  "In use" chunks have header: [size]
  "Free" chunks have header: [size][prev][next] and tail: [size]

  'size': [size_of_chunk:C:P]
  Since 'size' is aligned at least 4 bytes boundary, last 2 bits are not
  required and are used for special flags C and P:
  * C(Current) is 1 for chunks in use, 0 for free ones
  * B(Before) is 1 if chunk before this is in use, 0 if its free
    Only when B is 0, ptr->bsize may be used (chunk before is free chunk)

  Pointer 'prev' and 'next' do not require last 2 bits (because of alignment)
  so last bit of 'prev' is used to indicate if this is first chunk in list.
  Bit is marked as F. If F==1 then content of 'prev' is address of list header!
  E.g. list in variable: mchunk_t *list; 'prev' element for first element in
  list has address of 'list': x->prev = 1 + (void *) &list;
  This is used to get list header faster (without calculating 'fl' and 'sl').

  "In use" chunk:
	|							|
 chunk	+-------------------------------------------------------+++++++++++++++
 before	|	'size' of chunk before  (if its free)	    |x|x|  "in use"
     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-- chunk
	|		'size' of chunk			    |1|P|   header
	+-------------------------------------------------------+++++++++++++++
 in-use	|							|
 chunk	|	'size' - sizeof(size_t) available bytes		|
	|							|
	|							|++++++++++++++
	|							|   header
     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-- of next
 chunk	|		'size' of chunk after		    |x|1|   chunk
 after	+-------------------------------------------------------+
	|							|


  "Free" chunk:
	|							|
 chunk	|(chunk it's not free, adjacent free chunks are merged!)|++++++++++++++
 before	|							|
     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++--
	|		'size' of chunk			    |0|1|   "free"
	+-------------------------------------------------------+--  chunk
	|	pointer to previous free chunk in list	      |F|    header
	+-------------------------------------------------------+--
 free	|	pointer to next free chunk in list		|
 chunk	+-------------------------------------------------------+++++++++++++++
	|							|
	|	    unused part of chunk			|
	|							|
	+-------------------------------------------------------+
	|		'size' of chunk			    |0|1|
     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 chunk	|	     'size' of chunk after		    |1|0|
 after	+-------------------------------------------------------+
	|(chunk it's not free, adjacent free chunks are merged!)|

  To mark end of available memory (to allocator) special chunk is used:
  "border chunk" - chunk that consist only of header that has only size element,
  and its set to sizeof(size_t).
  This BORDER_CHUNK is placed on both side of memory segments used in allocator.
*/

/*! Constants and macros for chunk operations */

/*! Alignment constants and macros (macro heaven and hell) */
/* Align chunks sizes on following alignment (change '1' if required) */
#define MIN_ALIGN		( sizeof(size_t) >= 4 ? sizeof(size_t) : 4 )
#define CHUNK_ALIGN_VAL		( (size_t) MIN_ALIGN * 1 )
#define CHUNK_ALIGN_MASK	( ~( (size_t) ( CHUNK_ALIGN_VAL - 1 ) ) )
/* e.g. if size_t is 32 bit integer:
 CHUNK_ALIGN_VAL = 4, CHUNK_ALIGN_MASK = 0xfffffffc (binary: 111...11100) */
#define CHUNK_ALIGN(P)		( ((size_t)(P)) & CHUNK_ALIGN_MASK )
#define CHUNK_ALIGN_FW(P)	CHUNK_ALIGN(((size_t)(P))+(CHUNK_ALIGN_VAL-1))
#define CHUNK_IS_ALIGNED(P)	(!( ((size_t)(P)) & ( CHUNK_ALIGN_VAL - 1 ) ))

/*! Get-ers and Set-ers */
#define BINUSE		( (size_t) 1 )	/* Chunk "Before" IN USE */
#define CINUSE		( (size_t) 2 )	/* "Current" chunk IN USE */

#define GET_CHUNK_HDR_FROM_ADDR(ADR)         (((void *)(ADR))-sizeof(size_t))

#define GET_CHUNK_HDR_FROM_USABLE_ADDR(ADR)  (((void *)(ADR))-2*sizeof(size_t))
#define GET_CHUNK_USABLE_ADDR(HDR)           (((void *)(HDR))+2*sizeof(size_t))

#define GET_CHUNK_SIZE(CHUNK)       ( (CHUNK)->size & CHUNK_ALIGN_MASK )
#define SET_CHUNK_SIZE(CHUNK, SIZE) \
do { (CHUNK)->size = (SIZE) | ((CHUNK)->size & 3); } while(0)

#define GET_CHUNK_INUSE(CHUNK)       ( (CHUNK)->size & CINUSE )
#define SET_CHUNK_INUSE(CHUNK)       do { (CHUNK)->size |= CINUSE; } while(0)
#define CLEAR_CHUNK_INUSE(CHUNK)     do { (CHUNK)->size &= ~CINUSE; } while(0)

#define GET_CHUNK_BINUSE(CHUNK)      ( (CHUNK)->size & BINUSE )
#define SET_CHUNK_BINUSE(CHUNK)      do { (CHUNK)->size |= BINUSE; } while(0)
#define CLEAR_CHUNK_BINUSE(CHUNK)    do { (CHUNK)->size &= ~BINUSE; } while(0)

/*! Chunks before and after (by address) if its free; NULL otherwise */
#define GET_CHUNK_BEFORE(CHUNK)	\
( GET_CHUNK_BINUSE(CHUNK) ? NULL : \
((mchunk_t *) ( ((void *) (CHUNK)) - ((CHUNK)->bsize & CHUNK_ALIGN_MASK) )) )

#define GET_CHUNK_AFTER(CHUNK)	\
( (mchunk_t *) ( ((void *) (CHUNK)) + GET_CHUNK_SIZE(CHUNK) ) )

/* mark CHUNK as in use and set BINUSE flag in chunk after */
#define SET_CHUNK_IN_USE(CHUNK)	\
do{ SET_CHUNK_INUSE(CHUNK); SET_CHUNK_BINUSE(GET_CHUNK_AFTER(CHUNK));} while(0)

/* clone 'size' element to chunk tail */
#define CLONE_CHUNK_SIZE(CHUNK)	\
do { GET_CHUNK_AFTER(CHUNK)->bsize = (CHUNK)->size; } while(0)

#define JOIN_CHUNKS(C1, C2)	\
({ SET_CHUNK_SIZE((C1), GET_CHUNK_SIZE(C1) + GET_CHUNK_SIZE(C2)); (C1); })

/*! Operations on chunks in free list */
#define FIRST_IN_LIST(LIST)	(LIST)

#define GET_CHUNK_PREV(CHUNK)	\
((mchunk_t *) ( ((size_t) ((CHUNK)->prev)) & ~((size_t) 1) ))

#define IS_CHUNK_FIRST_IN_LIST(CHUNK)	( ((size_t) (CHUNK)->prev) & 1 )
#define SET_CHUNK_FIRST_IN_LIST(CHUNK)	\
do { (CHUNK)->prev = (mchunk_t *) ( ((size_t) (CHUNK)->prev) | 1 ); } while(0)

/*! Chunk & memory pool sizes */
#define MCHUNK_T_SZ		CHUNK_ALIGN_FW ( sizeof (mchunk_t) )

#define MIN_CHUNK_SIZE		\
( MCHUNK_T_SZ > CHUNK_ALIGN_VAL ? MCHUNK_T_SZ : CHUNK_ALIGN_VAL * 2)

#define MAX_CHUNK_SIZE		( ( ~( (size_t) 0 ) ) & CHUNK_ALIGN_MASK )

/* default minimum chunk size */
#define DEF_MIN_CHUNK_SIZE	( MIN_CHUNK_SIZE >= 32 ? MIN_CHUNK_SIZE : 32 )

/* If given memory segment is smaller than this we cannot create memory pool */
#define MIN_POOL_SIZE	( sizeof (gma_t) + 1 * sizeof (size_t) + \
	1 * SL_DIM * sizeof (size_t) + MIN_CHUNK_SIZE )

#define EXACT_LIMIT_SIZE_1	( 1 << ( 2 * L - 2 ) )	/* 2^(2L-2) */
#define EXACT_LIMIT_SIZE	\
	( EXACT_LIMIT_SIZE_1 * ( CHUNK_ALIGN_VAL / sizeof(size_t) ) )
/* Lists with chunks smaller than EXACT_LIMIT_SIZE have only one sized chunks
 * in list (due to chunk size alignment). Many lists in first few levels might
 * be unused because of alignment (if they are reserved for sizes that are not
 * aligned).
 */

/*! Border chunk */
#define BORDER_CHUNK_SIZE	CHUNK_ALIGN_VAL
#define BORDER_CHUNK		( BORDER_CHUNK_SIZE | CINUSE )
#define IS_BORDER_CHUNK(CHUNK)	( GET_CHUNK_SIZE(CHUNK) == BORDER_CHUNK_SIZE )
#define SET_BORDER_CHUNK(CHUNK)	\
do { (CHUNK)->size = BORDER_CHUNK; CLONE_CHUNK_SIZE(CHUNK); } while(0)

#define NEW_MPOOL	1

/*! mchunk list manipulations */
#include <lib/bits.h>
#ifndef ASSERT
#include <kernel/errno.h>
#endif

static void *make_first_chunk ( void *addr, size_t size )
{
	mchunk_t *chunk, *border;

	ASSERT ( addr && size > BORDER_CHUNK_SIZE * 2 + MIN_CHUNK_SIZE &&
		CHUNK_IS_ALIGNED ( (size_t) addr) );

	/* mark start of usable region */
	border = GET_CHUNK_HDR_FROM_ADDR ( addr );
	SET_BORDER_CHUNK ( border );

	/* space borders is first chunk */
	chunk = GET_CHUNK_HDR_FROM_ADDR ( addr + BORDER_CHUNK_SIZE );
	SET_CHUNK_SIZE ( chunk, size - 2 * BORDER_CHUNK_SIZE );
	SET_CHUNK_INUSE ( chunk ); /* mark chunk as in use */
	SET_CHUNK_BINUSE ( chunk ); /* previous is in use ("border" chunk) */

	/* mark end of usable region */
	border = GET_CHUNK_HDR_FROM_ADDR ( addr + size - BORDER_CHUNK_SIZE );
	SET_BORDER_CHUNK ( border );
	SET_CHUNK_BINUSE ( border );

	return GET_CHUNK_USABLE_ADDR ( chunk );
}

/* insert chunk at head of the list (as first element) */
static void insert_chunk_in_list ( mchunk_t **list, mchunk_t *chunk )
{
	mchunk_t *after;

	chunk->next = *list;
	if ( *list )
		(*list)->prev = chunk;
	*list = chunk;

	chunk->prev = (mchunk_t *) list; /* first in list */
	SET_CHUNK_FIRST_IN_LIST ( chunk );

	CLEAR_CHUNK_INUSE ( chunk );
	CLONE_CHUNK_SIZE ( chunk );

	/* clear BINUSE bit in chunk after */
	after = GET_CHUNK_AFTER ( chunk );
	CLEAR_CHUNK_BINUSE ( after );
	//if ( !GET_CHUNK_INUSE ( after ) )
	//	CLONE_CHUNK_SIZE ( after ); //BINUSE doesn't matter in tail
}

/*!
 * Remove chunk from list
 * \param chunk Pointer to chunk header
 * \returns 0 if list still not empty after removing this chunk, otherwise chunk
 *          size is returned (to mark that list as empty)
 */
static size_t remove_chunk_from_list ( mchunk_t *chunk )
{
	mchunk_t **list;
	size_t ret_value = 0;

	if ( IS_CHUNK_FIRST_IN_LIST ( chunk ) )
	{
		list = (mchunk_t **) GET_CHUNK_PREV ( chunk );

		*list = chunk->next;
		if ( *list )
		{
			(*list)->prev = (mchunk_t *) list ;
			SET_CHUNK_FIRST_IN_LIST ( *list );
		}
		else {
			ret_value = 1;
		}
	}
	else {
		chunk->prev->next = chunk->next;
		if ( chunk->next )
			chunk->next->prev = chunk->prev;
	}

	return ret_value;
}

static mchunk_t *split_chunk_at ( mchunk_t *chunk, size_t size )
{
	mchunk_t *remainder;

	remainder = ( (void *) chunk ) + size;
	remainder->size = 0; /* reset CINUSE and BINUSE */
	SET_CHUNK_SIZE ( remainder, GET_CHUNK_SIZE ( chunk ) - size );
	SET_CHUNK_SIZE ( chunk, size ); /* CINUSE and BINUSE are preserved */

	return remainder;
}


/*! 'gma' functions */
gma_t *gma_init ( void *memory_segment, size_t size, size_t min_chunk_size,
		  uint flags );
void *gma_alloc ( gma_t *mpool, size_t size );
int gma_free ( gma_t *mpool, void *address );
static int get_indexes(gma_t *mpool,size_t size,size_t *fl,size_t *sl,int ins);
static inline void set_list_have_chunks ( gma_t *mpool, size_t fl, size_t sl );
static inline void clear_list_have_chunks (gma_t *mpool, size_t fl, size_t sl);
static void insert_chunk_in_free_list ( gma_t *mpool, mchunk_t *chunk );
static void remove_chunk_from_free_list ( gma_t *mpool, mchunk_t *chunk );
static mchunk_t *remove_first_chunk_from_free_list ( gma_t *mpool, size_t fl,
						     size_t sl );

/* ToDo:
   int extend_mpool ( gma_t *mpool, size_t size_to_add_to_the_end_of_mpool );
   int shrink_mpool ( gma_t *mpool, size_t size_at_end_of_mpool_to_release );
*/
#endif /* _GMA_C_ */
