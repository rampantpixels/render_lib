/* backend.c  -  Render library  -  Public Domain  -  2014 Mattias Jansson / Rampant Pixels
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
 * This library is put in the public domain; you can redistribute it and/or modify it without any
 * restrictions.
 *
 */

#include <foundation/foundation.h>

#include <render/render.h>
#include <render/internal.h>

#include <render/null/backend.h>
#include <render/gl2/backend.h>
#include <render/gl4/backend.h>
#include <render/gles2/backend.h>

#include <resource/platform.h>

FOUNDATION_DECLARE_THREAD_LOCAL(render_backend_t*, backend, nullptr)

static render_api_t
render_api_fallback(render_api_t api) {
	switch (api) {
		case RENDERAPI_UNKNOWN:
			return RENDERAPI_UNKNOWN;

		case RENDERAPI_DEFAULT:
#if FOUNDATION_PLATFORM_WINDOWS
			return RENDERAPI_DIRECTX;
#elif FOUNDATION_PLATFORM_IOS || FOUNDATION_PLATFORM_ANDROID || \
    FOUNDATION_PLATFORM_LINUX_RASPBERRYPI
			return RENDERAPI_GLES;
#else
			return RENDERAPI_OPENGL;
#endif

		case RENDERAPI_NULL:
			return RENDERAPI_UNKNOWN;

		case RENDERAPI_OPENGL:
			return RENDERAPI_OPENGL4;
		case RENDERAPI_DIRECTX:
			return RENDERAPI_DIRECTX11;
		case RENDERAPI_GLES:
			return RENDERAPI_GLES3;

#if FOUNDATION_PLATFORM_WINDOWS
		case RENDERAPI_OPENGL4:
			return RENDERAPI_DIRECTX10;
#else
		case RENDERAPI_OPENGL4:
			return RENDERAPI_OPENGL2;
#endif
		case RENDERAPI_DIRECTX10:
			return RENDERAPI_OPENGL2;
		case RENDERAPI_DIRECTX11:
			return RENDERAPI_OPENGL4;
		case RENDERAPI_GLES3:
			return RENDERAPI_GLES2;
		case RENDERAPI_GLES2:
			return RENDERAPI_NULL;
		case RENDERAPI_OPENGL2:
			return RENDERAPI_NULL;

		case RENDERAPI_PS3:
		case RENDERAPI_PS4:
		case RENDERAPI_XBOX360:
		case RENDERAPI_XBOXONE:
		case RENDERAPI_NUM:
			return RENDERAPI_NULL;

		default:
			break;
	}
	return RENDERAPI_UNKNOWN;
}

render_backend_t**
render_backends(void) {
	return _render_backends;
}

render_backend_t*
render_backend_allocate(render_api_t api, bool allow_fallback) {
	// First find best matching supported backend
	render_backend_t* backend = 0;

	memory_context_push(HASH_RENDER);

	while (!backend) {
		while (_render_api_disabled[api])
			api = render_api_fallback(api);
		switch (api) {
			case RENDERAPI_GLES2:
				backend = render_backend_gles2_allocate();
				if (!backend || !backend->vtable.construct(backend)) {
					log_info(HASH_RENDER,
					         STRING_CONST("Failed to initialize OpenGL ES 2 render backend"));
					render_backend_deallocate(backend);
					backend = nullptr;
				}
				break;

			case RENDERAPI_GLES3:
				/*backend = render_backend_gles3_allocate();
				if (!backend || !backend->vtable.construct(backend)) {
				    log_info(HASH_RENDER, STRING_CONST("Failed to initialize OpenGL ES 3 render
				backend")); render_backend_deallocate(backend), backend = 0;
				}*/
				break;

			case RENDERAPI_OPENGL2:
				backend = render_backend_gl2_allocate();
				if (!backend || !backend->vtable.construct(backend)) {
					log_info(HASH_RENDER,
					         STRING_CONST("Failed to initialize OpenGL 2 render backend"));
					render_backend_deallocate(backend);
					backend = nullptr;
				}
				break;

			case RENDERAPI_OPENGL4:
				backend = render_backend_gl4_allocate();
				if (!backend || !backend->vtable.construct(backend)) {
					log_info(HASH_RENDER,
					         STRING_CONST("Failed to initialize OpenGL 4 render backend"));
					render_backend_deallocate(backend);
					backend = nullptr;
				}
				break;

			case RENDERAPI_DIRECTX10:
				/*backend = render_dx10_allocate();
				if( !backend || !backend->vtable.construct( backend ) )
				{
				    log_info( HASH_RENDER, "Failed to initialize DirectX 10 render backend" );
				    render_deallocate( backend ), backend = 0;
				}*/
				break;

			case RENDERAPI_DIRECTX11:
				/*backend = render_dx11_allocate();
				if( !backend || !backend->vtable.construct( backend ) )
				{
				    log_info( HASH_RENDER, "Failed to initialize DirectX 11 render backend" );
				    render_deallocate( backend ), backend = 0;
				}*/
				break;

			case RENDERAPI_NULL:
				backend = render_backend_null_allocate();
				backend->vtable.construct(backend);
				break;

			case RENDERAPI_UNKNOWN:
				log_warn(HASH_RENDER, WARNING_SUSPICIOUS,
				         STRING_CONST("No supported and enabled render api found, giving up"));
				return 0;

			case RENDERAPI_PS3:
			case RENDERAPI_PS4:
			case RENDERAPI_XBOX360:
			case RENDERAPI_XBOXONE:
				// Try loading dynamic library
				log_warnf(HASH_RENDER, WARNING_SUSPICIOUS,
				          STRING_CONST("Render API not yet implemented (%u)"), api);
				break;

			case RENDERAPI_NUM:
			case RENDERAPI_DEFAULT:
			case RENDERAPI_OPENGL:
			case RENDERAPI_DIRECTX:
			case RENDERAPI_GLES:
			default:
				// Try loading dynamic library
				log_warnf(
				    HASH_RENDER, WARNING_SUSPICIOUS,
				    STRING_CONST(
				        "Unknown render API (%u), dynamic library loading not implemented yet"),
				    api);
				break;
		}

		if (!backend) {
			if (!allow_fallback) {
				log_warn(HASH_RENDER, WARNING_UNSUPPORTED,
				         STRING_CONST("Requested render api not supported"));
				return 0;
			}

			api = render_api_fallback(api);
		}
	}

	backend->exclusive = mutex_allocate(STRING_CONST("render_backend_exclusive"));

	render_target_initialize_framebuffer(&backend->framebuffer, backend);
	backend->framecount = 1;

	uuidmap_initialize((uuidmap_t*)&backend->shadertable,
	                   sizeof(backend->shadertable.bucket) / sizeof(backend->shadertable.bucket[0]),
	                   0);
	uuidmap_initialize(
	    (uuidmap_t*)&backend->programtable,
	    sizeof(backend->programtable.bucket) / sizeof(backend->programtable.bucket[0]), 0);
	uuidmap_initialize(
	    (uuidmap_t*)&backend->texturetable,
	    sizeof(backend->texturetable.bucket) / sizeof(backend->texturetable.bucket[0]), 0);

	render_backend_set_resource_platform(backend, 0);

	array_push(_render_backends, backend);

	memory_context_pop();

	render_backend_enable_thread(backend);

	return backend;
}

void
render_backend_deallocate(render_backend_t* backend) {
	if (!backend)
		return;

	backend->vtable.destruct(backend);

	uuidmap_finalize((uuidmap_t*)&backend->shadertable);
	uuidmap_finalize((uuidmap_t*)&backend->programtable);
	uuidmap_finalize((uuidmap_t*)&backend->texturetable);

	render_target_finalize(&backend->framebuffer);
	render_drawable_finalize(&backend->drawable);

	mutex_deallocate(backend->exclusive);

	for (size_t ib = 0, bsize = array_size(_render_backends); ib < bsize; ++ib) {
		if (_render_backends[ib] == backend) {
			array_erase(_render_backends, ib);
			break;
		}
	}

	memory_deallocate(backend);
}

render_api_t
render_backend_api(render_backend_t* backend) {
	return backend ? backend->api : RENDERAPI_UNKNOWN;
}

size_t
render_backend_enumerate_adapters(render_backend_t* backend, unsigned int* store, size_t capacity) {
	return backend->vtable.enumerate_adapters(backend, store, capacity);
}

size_t
render_backend_enumerate_modes(render_backend_t* backend, unsigned int adapter,
                               render_resolution_t* store, size_t capacity) {
	return backend->vtable.enumerate_modes(backend, adapter, store, capacity);
}

bool
render_backend_try_enter_exclusive(render_backend_t* backend) {
	return mutex_try_lock(backend->exclusive);
}

void
render_backend_enter_exclusive(render_backend_t* backend) {
	mutex_lock(backend->exclusive);
}

void
render_backend_leave_exclusive(render_backend_t* backend) {
	mutex_unlock(backend->exclusive);
}

void
render_backend_set_format(render_backend_t* backend, const pixelformat_t format,
                          const colorspace_t space) {
	if (!FOUNDATION_VALIDATE_MSG(!backend->drawable.type,
	                             "Unable to change format when drawable is already set"))
		return;
	backend->pixelformat = format;
	backend->colorspace = space;
}

bool
render_backend_set_drawable(render_backend_t* backend, const render_drawable_t* drawable) {
	render_backend_t* prev_backend = get_thread_backend();
	if (prev_backend && (prev_backend != backend))
		prev_backend->vtable.disable_thread(prev_backend);

	if (!backend->vtable.set_drawable(backend, drawable))
		return false;

	render_drawable_finalize(&backend->drawable);
	if (drawable->type == RENDERDRAWABLE_WINDOW)
		render_drawable_initialize_window(&backend->drawable, drawable->window, drawable->tag);
	else if (drawable->type == RENDERDRAWABLE_FULLSCREEN)
		render_drawable_initialize_fullscreen(&backend->drawable, drawable->adapter,
		                                      drawable->width, drawable->height, drawable->refresh);

	backend->framebuffer.width = backend->drawable.width;
	backend->framebuffer.height = backend->drawable.height;
	backend->framebuffer.pixelformat = backend->pixelformat;
	backend->framebuffer.colorspace = backend->colorspace;

	set_thread_backend(backend);

	return true;
}

render_drawable_t*
render_backend_drawable(render_backend_t* backend) {
	return &backend->drawable;
}

render_target_t*
render_backend_target_framebuffer(render_backend_t* backend) {
	return &backend->framebuffer;
}

void
render_backend_dispatch(render_backend_t* backend, render_target_t* target,
                        render_context_t** contexts, size_t num_contexts) {
	backend->vtable.dispatch(backend, target, contexts, num_contexts);

	for (size_t i = 0; i < num_contexts; ++i)
		atomic_store32(&contexts[i]->reserved, 0, memory_order_release);
}

void
render_backend_flip(render_backend_t* backend) {
	backend->vtable.flip(backend);
}

uint64_t
render_backend_frame_count(render_backend_t* backend) {
	return backend->framecount;
}

void
render_backend_enable_thread(render_backend_t* backend) {
	render_backend_t* prev_backend = get_thread_backend();
	if (prev_backend && (prev_backend != backend))
		prev_backend->vtable.disable_thread(prev_backend);
	set_thread_backend(backend);
	backend->vtable.enable_thread(backend);
}

void
render_backend_disable_thread(render_backend_t* backend) {
	render_backend_t* prev_backend = get_thread_backend();
	backend->vtable.disable_thread(backend);
	if (prev_backend == backend)
		set_thread_backend(nullptr);
}

void
render_backend_set_max_concurrency(render_backend_t* backend, size_t num_threads) {
	if (backend->concurrency || backend->drawable.width)
		return;
	backend->concurrency = (uint64_t)num_threads;
}

size_t
render_backend_max_concurrency(render_backend_t* backend) {
	return (size_t)backend->concurrency;
}

render_backend_t*
render_backend_thread(void) {
	return get_thread_backend();
}

uint64_t
render_backend_resource_platform(render_backend_t* backend) {
	return backend->platform;
}

void
render_backend_set_resource_platform(render_backend_t* backend, uint64_t platform) {
	resource_platform_t decl = resource_platform_decompose(platform);
	decl.render_api_group = (int)backend->api_group;
	decl.render_api = (int)backend->api;
	backend->platform = resource_platform(decl);
}

uuidmap_t*
render_backend_shader_table(render_backend_t* backend) {
	return (uuidmap_t*)&backend->shadertable;
}

uuidmap_t*
render_backend_program_table(render_backend_t* backend) {
	return (uuidmap_t*)&backend->programtable;
}

uuidmap_t*
render_backend_texture_table(render_backend_t* backend) {
	return (uuidmap_t*)&backend->texturetable;
}

bool
render_backend_shader_upload(render_backend_t* backend, render_shader_t* shader, const void* buffer,
                             size_t size) {
	if (shader->backend && (shader->backend != backend))
		shader->backend->vtable.deallocate_shader(shader->backend, shader);
	shader->backend = nullptr;
	if (backend->vtable.upload_shader(backend, shader, buffer, size)) {
		shader->backend = backend;
		return true;
	}
	return false;
}

bool
render_backend_program_upload(render_backend_t* backend, render_program_t* program) {
	if (program->backend && (program->backend != backend))
		program->backend->vtable.deallocate_program(program->backend, program);
	program->backend = nullptr;
	if (backend->vtable.upload_program(backend, program)) {
		program->backend = backend;
		return true;
	}
	return false;
}

bool
render_backend_texture_upload(render_backend_t* backend, render_texture_t* texture,
                              const void* buffer, size_t size) {
	if (texture->backend && (texture->backend != backend))
		texture->backend->vtable.deallocate_texture(texture->backend, texture);
	texture->backend = nullptr;
	if (backend->vtable.upload_texture(backend, texture, buffer, size)) {
		texture->backend = backend;
		return true;
	}
	return false;
}

void
render_backend_parameter_bind_texture(render_backend_t* backend, void* buffer,
                                      render_texture_t* texture) {
	backend->vtable.parameter_bind_texture(backend, buffer, texture);
}

void
render_backend_parameter_bind_target(render_backend_t* backend, void* buffer,
                                     render_target_t* target) {
	backend->vtable.parameter_bind_target(backend, buffer, target);
}
