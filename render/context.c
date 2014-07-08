/* context.c  -  Render library  -  Public Domain  -  2014 Mattias Jansson / Rampant Pixels
 *
 * This library provides a cross-platform rendering library in C11 providing
 * basic 2D/3D rendering functionality for projects based on our foundation library.
 *
 * The latest source code maintained by Rampant Pixels is always available at
 *
 * https://github.com/rampantpixels/render_lib
 *
 * The foundation library source code maintained by Rampant Pixels is always available at
 *
 * https://github.com/rampantpixels/foundation_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */


#include <foundation/foundation.h>

#include <render/render.h>
#include <render/internal.h>


render_context_t* render_context_allocate( unsigned int commands )
{
	render_context_t* context;
	
	FOUNDATION_ASSERT( commands < (radixsort_index_t)-1 );

	memory_context_push( HASH_RENDER );
	
	context = memory_allocate_zero( sizeof( render_context_t ), 0, MEMORY_PERSISTENT );
	
	context->allocated = commands;
	context->commands  = memory_allocate( sizeof( render_command_t ) * commands, 0, MEMORY_PERSISTENT );
	context->keys      = memory_allocate( sizeof( uint64_t ) * commands, 0, MEMORY_PERSISTENT );
	context->sort      = radixsort_allocate( RADIXSORT_UINT64, (radixsort_index_t)commands );
	
	return context;
}


void render_context_deallocate( render_context_t* context )
{
	render_target_destroy( context->target );
	radixsort_deallocate( context->sort );
	memory_deallocate( context->commands );
	memory_deallocate( context->keys );
	memory_deallocate( context );
}


object_t render_context_target( render_context_t* context )
{
	return context->target;
}


void render_context_set_target( render_context_t* context, object_t target )
{
	render_target_ref( target );
	render_target_destroy( context->target );
	context->target = target;
}


render_command_t* render_context_reserve( render_context_t* context, uint64_t sort )
{
	int32_t idx = atomic_exchange_and_add32( &context->reserved, 1 );
	FOUNDATION_ASSERT_MSG( idx < context->allocated, "Render command overallocation" );
	context->keys[ idx ] = sort;
	return context->commands + idx;
}


void render_context_queue( render_context_t* context, render_command_t* command, uint64_t sort )
{
	int32_t idx = atomic_exchange_and_add32( &context->reserved, 1 );
	FOUNDATION_ASSERT_MSG( idx < context->allocated, "Render command overallocation" );
	context->keys[ idx ] = sort;
	memcpy( context->commands + idx, command, sizeof( render_command_t ) );
}


unsigned int render_context_reserved( render_context_t* context )
{
	return atomic_load32( &context->reserved );
}
