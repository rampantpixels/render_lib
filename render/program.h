/* program.h  -  Render library  -  Public Domain  -  2015 Mattias Jansson / Rampant Pixels
 *
 * This library provides a cross-platform rendering library in C11 providing
 * basic 2D/3D rendering functionality for projects based on our foundation library.
 *
 * The latest source code maintained by Rampant Pixels is always available at
 *
 * https://github.com/rampantpixels/render_lib
 *
 * The dependent library source code maintained by Rampant Pixels is always available at
 *
 * https://github.com/rampantpixels
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#pragma once

/*! \file program.h
    Programmable render pipeline program */

#include <foundation/platform.h>

#include <render/types.h>

RENDER_API render_program_t*
render_program_allocate(size_t num_parameters);

RENDER_API void
render_program_initialize(render_program_t* program, size_t num_parameters);

RENDER_API void
render_program_finalize(render_program_t* program);

RENDER_API void
render_program_deallocate(render_program_t* program);

RENDER_API bool
render_program_upload(render_backend_t* backend, render_program_t* program);

RENDER_API render_program_t*
render_program_load(render_backend_t* backend, const uuid_t uuid);

#if RESOURCE_ENABLE_LOCAL_SOURCE

/* Compile program resource
\param uuid Program UUID
\param platform Resource platform
\param source Program resource source representation
\param type Type string
\param type_length Length of type string
\return 0 if successful, <0 if error */
RENDER_API int
render_program_compile(const uuid_t uuid, uint64_t platform, resource_source_t* source,
                       const char* type, size_t type_length);

#else

#define render_program_compile(uuid, platform, source, type, type_length) ((void)sizeof(uuid)), ((void)sizeof(platform)), ((void)sizeof(source)), ((void)sizeof(type)), ((void)sizeof(type_length)), -1

#endif