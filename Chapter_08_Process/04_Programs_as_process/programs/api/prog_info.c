/* Program info */

#include "prog_info.h"
#include <api/thread.h>
#include <api/malloc.h>

/* symbols from user.ld */
extern char user_code, user_end, user_heap, user_stack;

extern int PROG_START_FUNC ( char *args[] );

prog_info_t pi =
{
	.zero =		{ 0, 0, 0, 0 },
	.init = 	prog_init,
	.entry =	PROG_START_FUNC,
	.param =	NULL,
	.exit =		thread_exit,
	.prio =		THR_DEFAULT_PRIO,

	.start_adr =	&user_code,
	.heap =		&user_heap,
	.stack =	&user_stack,
	.end_adr =	&user_end,

	.mpool =	NULL,
	.stdin =	NULL,
	.stdout =	NULL
};

/*! Initialize process environment */
void prog_init ( void *args )
{
	/* initialize dynamic memory */
	pi.mpool = mem_init ( pi.heap, (size_t) pi.stack - (size_t) pi.heap );

	/* call starting function */
	( (void (*) ( void * ) ) pi.entry ) ( args );

	thread_exit ( 0 );
}
