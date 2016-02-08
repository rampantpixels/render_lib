/* compile.c  -  Render library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

#include <foundation/foundation.h>
#include <render/render.h>
#include <resource/resource.h>
#include <window/window.h>

#include "gl4/glwrap.h"
#include "gl4/glprocs.h"

#if RESOURCE_ENABLE_LOCAL_SOURCE

int
render_compile(const uuid_t uuid, uint64_t platform, resource_source_t* source,
               const char* type, size_t type_length) {
	if (render_shader_compile(uuid, platform, source, type, type_length) == 0)
		return 0;
	if (render_program_compile(uuid, platform, source, type, type_length) == 0)
		return 0;
	return -1;
}

static resource_change_t*
resource_source_platform_reduce(resource_change_t* change, resource_change_t* best, void* data) {
	uint64_t** subplatforms = data;
	uint64_t platform = (*subplatforms)[0];
	size_t iplat, psize;
	if ((platform == RESOURCE_PLATFORM_ALL) ||
	        resource_platform_is_equal_or_more_specific(change->platform, platform)) {
		for (iplat = 1, psize = array_size(*subplatforms); iplat != psize; ++iplat) {
			if ((*subplatforms)[iplat] == change->platform)
				break;
		}
		if (iplat >= psize)
			array_push(*subplatforms, change->platform);
	}
	return 0;
}

int
render_shader_compile(const uuid_t uuid, uint64_t platform, resource_source_t* source,
                      const char* type, size_t type_length) {
	int result = 0;
	uint64_t* subplatforms = 0;
	size_t iplat, psize;
	hashmap_fixed_t fixedmap;
	hashmap_t* map = (hashmap_t*)&fixedmap;
	resource_platform_t platform_decl;
	render_backend_t* backend = 0;
	window_t* window = 0;
	render_drawable_t* drawable = 0;
	hash_t resource_type_hash;

	resource_type_hash = hash(type, type_length);

	if ((resource_type_hash != HASH_VERTEXSHADER) && (resource_type_hash != HASH_PIXELSHADER))
		return -1;

	array_push(subplatforms, platform);

	hashmap_initialize(map, sizeof(fixedmap.bucket) / sizeof(fixedmap.bucket[0]), 8);
	resource_source_map_all(source, map, false);
	resource_source_map_reduce(source, map, &subplatforms, resource_source_platform_reduce);
	resource_source_map_clear(map);

	for (iplat = 1, psize = array_size(subplatforms); (iplat != psize) && (result == 0); ++iplat) {
		void* compiled_blob = 0;
		size_t compiled_size = 0;
		stream_t* stream;
		uint64_t subplatform = subplatforms[iplat];
		if (subplatform == 0)
			continue; //Shaders are always platform specific

		platform_decl = resource_platform_decompose(subplatform);
		if (platform_decl.render_api <= RENDERAPI_DEFAULT) {
			if (platform_decl.render_api_group == RENDERAPIGROUP_OPENGL)
				platform_decl.render_api = RENDERAPI_OPENGL;
			else if (platform_decl.render_api_group == RENDERAPIGROUP_DIRECTX)
				platform_decl.render_api = RENDERAPI_DIRECTX;
			else if (platform_decl.render_api_group == RENDERAPIGROUP_GLES)
				platform_decl.render_api = RENDERAPI_GLES;
			else
				continue; //Nonspecific render api
		}

		backend = render_backend_allocate(platform_decl.render_api, true);
		if (!backend) {
			log_warn(HASH_RESOURCE, WARNING_UNSUPPORTED,
			         STRING_CONST("Unable to create render backend for shader compilation"));
			result = -1;
			continue;
		}

#if FOUNDATION_PLATFORM_WINDOWS
		window = window_create(WINDOW_ADAPTER_DEFAULT, STRING_CONST("Render compile"), 100, 100, false);
#endif

		drawable = render_drawable_allocate();
		render_drawable_set_window(drawable, window);

		render_backend_set_format(backend, PIXELFORMAT_R8G8B8X8, COLORSPACE_LINEAR);
		render_backend_set_drawable(backend, drawable);

		if ((platform_decl.render_api >= RENDERAPI_OPENGL) &&
		        (platform_decl.render_api <= RENDERAPI_OPENGL4)) {
			char* sourcebuffer = 0;
			resource_change_t* sourcechange = resource_source_get(source, HASH_SOURCE, subplatform);
			if (sourcechange && (sourcechange->flags & RESOURCE_SOURCEFLAG_BLOB)) {
				sourcebuffer = memory_allocate(HASH_RESOURCE, sourcechange->value.blob.size, 0, MEMORY_PERSISTENT);
				if (!resource_source_read_blob(uuid, HASH_SOURCE, subplatform, sourcechange->value.blob.checksum,
				                               sourcebuffer, sourcechange->value.blob.size)) {
					memory_deallocate(sourcebuffer);
					sourcebuffer = 0;
				}
			}
			if (sourcebuffer) {
				GLuint handle = glCreateShader(
				                    string_equal(type, type_length, STRING_CONST("vertexshader")) ?
				                    GL_VERTEX_SHADER_ARB : GL_FRAGMENT_SHADER_ARB);
				GLchar* glsource = (GLchar*)sourcebuffer;
				GLint source_size = (GLint)sourcechange->value.blob.size;
				glShaderSource(handle, 1, &glsource, &source_size);
				glCompileShader(handle);

				size_t log_capacity = 2048;
				GLchar* log_buffer = memory_allocate(HASH_RESOURCE, log_capacity, 0, MEMORY_TEMPORARY);
				GLint log_length = (GLint)log_capacity;
				GLint compiled = 0;
				glGetShaderiv(handle, GL_COMPILE_STATUS, &compiled);
				glGetShaderInfoLog(handle, log_length, &log_length, log_buffer);
				if (!compiled) {
					log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Unable to compile shader: %.*s"),
					           (int)log_length, log_buffer);
					result = -1;
				}
				else {
					if (log_length < 2) {
						string_t nomsg = string_copy(log_buffer, log_capacity, STRING_CONST("<no message>"));
						log_length = (GLint)nomsg.length;
					}
					log_debugf(HASH_RESOURCE, STRING_CONST("Successfully compiled shader: %.*s"), (int)log_length,
					           log_buffer);

					compiled_size = sourcechange->value.blob.size;
					compiled_blob = memory_allocate(HASH_RESOURCE, compiled_size, 0, MEMORY_PERSISTENT);
					memcpy(compiled_blob, sourcebuffer, compiled_size);
				}
				memory_deallocate(log_buffer);
				memory_deallocate(sourcebuffer);
				glDeleteShader(handle);
			}
		}

		render_backend_deallocate(backend);
		render_drawable_deallocate(drawable);
		window_deallocate(window);

		if (compiled_size <= 0)
			continue;

		stream = resource_local_create_static(uuid, subplatform);
		if (stream) {
			uint32_t version = 1;
			stream_write_uint64(stream, resource_type_hash);
			stream_write_uint32(stream, version);
			if (resource_type_hash == HASH_VERTEXSHADER) {
				render_vertexshader_t shader;
				render_vertexshader_initialize(&shader);
				stream_write(stream, &shader, sizeof(shader));
			}
			else {
				render_pixelshader_t shader;
				render_pixelshader_initialize(&shader);
				stream_write(stream, &shader, sizeof(shader));
			}
			stream_deallocate(stream);

			if (compiled_size > 0) {
				stream = resource_local_create_dynamic(uuid, subplatform);
				if (stream) {
					stream_write_uint32(stream, version);
					stream_write_uint64(stream, compiled_size);
					stream_write(stream, compiled_blob, compiled_size);
					stream_deallocate(stream);
				}
				else {
					log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL,
					           STRING_CONST("Unable to create dynamic resource stream"));
					result = -1;
				}
			}
		}
		else {
			log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL,
			           STRING_CONST("Unable to create static resource stream"));
			result = -1;
		}

		memory_deallocate(compiled_blob);
	}

	return result;
}

int
render_program_compile(const uuid_t uuid, uint64_t platform, resource_source_t* source,
                       const char* type, size_t type_length) {
	int result = 0;
	hash_t resource_type_hash;
	uint64_t* subplatforms = 0;
	uint64_t* moreplatforms = 0;
	size_t iplat, psize;
	size_t imore, moresize;
	hashmap_fixed_t fixedmap;
	hashmap_t* map = (hashmap_t*)&fixedmap;
	resource_platform_t platform_decl;
	render_backend_t* backend = 0;
	window_t* window = 0;
	render_drawable_t* drawable = 0;

	resource_type_hash = hash(type, type_length);

	if (resource_type_hash != HASH_PROGRAM)
		return -1;

	array_push(subplatforms, platform);

	hashmap_initialize(map, sizeof(fixedmap.bucket) / sizeof(fixedmap.bucket[0]), 8);
	resource_source_map_all(source, map, false);
	resource_source_map_reduce(source, map, &subplatforms, resource_source_platform_reduce);
	resource_source_map_clear(map);

	//First make sure we catch specialized platforms from shaders since
	//programs are the sum of the shaders
	for (iplat = 1, psize = array_size(subplatforms); (iplat != psize) && (result == 0); ++iplat) {
		uuid_t vertexshader, pixelshader;
		uint64_t subplatform = subplatforms[iplat];

		resource_source_t shadersource;
		uint64_t* shaderplatforms = 0;
		size_t ishaderplat, pshadersize;

		resource_change_t* shaderchange = resource_source_get(source, HASH_VERTEXSHADER, subplatform);
		if (!shaderchange || !(shaderchange->flags & RESOURCE_SOURCEFLAG_VALUE)) {
			log_error(HASH_RESOURCE, ERROR_INVALID_VALUE,
			          STRING_CONST("Unable to compile program: Missing vertex shader"));
			result = -1;
			continue;
		}
		vertexshader = string_to_uuid(STRING_ARGS(shaderchange->value.value));

		shaderchange = resource_source_get(source, HASH_PIXELSHADER, subplatform);
		if (!shaderchange || !(shaderchange->flags & RESOURCE_SOURCEFLAG_VALUE)) {
			log_error(HASH_RESOURCE, ERROR_INVALID_VALUE,
			          STRING_CONST("Unable to compile program: Missing pixel shader"));
			result = -1;
			continue;
		}
		pixelshader = string_to_uuid(STRING_ARGS(shaderchange->value.value));

		//Get additional more specialized platform targets from shaders
		array_push(shaderplatforms, subplatform);

		resource_source_initialize(&shadersource);
		if (resource_source_read(&shadersource, vertexshader)) {
			resource_source_map_all(&shadersource, map, false);
			resource_source_map_reduce(&shadersource, map, &shaderplatforms, resource_source_platform_reduce);
			resource_source_map_clear(map);
		}
		resource_source_finalize(&shadersource);

		resource_source_initialize(&shadersource);
		if (resource_source_read(&shadersource, pixelshader)) {
			resource_source_map_all(&shadersource, map, false);
			resource_source_map_reduce(&shadersource, map, &shaderplatforms, resource_source_platform_reduce);
			resource_source_map_clear(map);
		}
		resource_source_finalize(&shadersource);

		for (ishaderplat = 1, pshadersize = array_size(shaderplatforms); ishaderplat != pshadersize;
		        ++ishaderplat) {
			uint64_t moreplatform = shaderplatforms[ishaderplat];
			if ((moreplatform != subplatform) &&
			        resource_platform_is_equal_or_more_specific(moreplatform, subplatform)) {
				for (imore = 0, moresize = array_size(moreplatforms); imore != moresize; ++imore) {
					if (moreplatforms[imore] == moreplatform)
						break;
				}
				if (imore == moresize)
					array_push(moreplatforms, shaderplatforms[ishaderplat]);
			}
		}
	}

	for (imore = 0, moresize = array_size(moreplatforms); imore != moresize; ++imore) {
		for (iplat = 1, psize = array_size(subplatforms); (iplat != psize) && (result == 0); ++iplat) {
			if (moreplatforms[imore] == subplatforms[iplat])
				break;
		}
		if (iplat == psize)
			array_push(subplatforms, moreplatforms[imore]);
	}

	for (iplat = 1, psize = array_size(subplatforms); (iplat != psize) && (result == 0); ++iplat) {
		stream_t* stream;
		uuid_t vertexshader, pixelshader;
		render_program_t* program = 0;
		uint64_t subplatform = subplatforms[iplat];
		if (subplatform == 0)
			continue; //Programs are always platform specific

		platform_decl = resource_platform_decompose(subplatform);
		if (platform_decl.render_api <= RENDERAPI_DEFAULT) {
			if (platform_decl.render_api_group == RENDERAPIGROUP_OPENGL)
				platform_decl.render_api = RENDERAPI_OPENGL;
			else if (platform_decl.render_api_group == RENDERAPIGROUP_DIRECTX)
				platform_decl.render_api = RENDERAPI_DIRECTX;
			else if (platform_decl.render_api_group == RENDERAPIGROUP_GLES)
				platform_decl.render_api = RENDERAPI_GLES;
			else
				continue; //Nonspecific render api
		}

		backend = render_backend_allocate(platform_decl.render_api, true);
		if (!backend) {
			log_warn(HASH_RESOURCE, WARNING_UNSUPPORTED,
			         STRING_CONST("Unable to create render backend for shader compilation"));
			result = -1;
			continue;
		}

#if FOUNDATION_PLATFORM_WINDOWS
		window = window_create(WINDOW_ADAPTER_DEFAULT, STRING_CONST("Render compile"), 100, 100, false);
#endif

		drawable = render_drawable_allocate();
		render_drawable_set_window(drawable, window);

		render_backend_set_format(backend, PIXELFORMAT_R8G8B8X8, COLORSPACE_LINEAR);
		render_backend_set_drawable(backend, drawable);

		resource_change_t* shaderchange = resource_source_get(source, HASH_VERTEXSHADER, subplatform);
		if (!shaderchange || !(shaderchange->flags & RESOURCE_SOURCEFLAG_VALUE)) {
			log_error(HASH_RESOURCE, ERROR_INVALID_VALUE,
			          STRING_CONST("Unable to compile program: Missing vertex shader"));
			result = -1;
			continue;
		}
		vertexshader = string_to_uuid(STRING_ARGS(shaderchange->value.value));

		shaderchange = resource_source_get(source, HASH_PIXELSHADER, subplatform);
		if (!shaderchange || !(shaderchange->flags & RESOURCE_SOURCEFLAG_VALUE)) {
			log_error(HASH_RESOURCE, ERROR_INVALID_VALUE,
			          STRING_CONST("Unable to compile program: Missing pixel shader"));
			result = -1;
			continue;
		}
		pixelshader = string_to_uuid(STRING_ARGS(shaderchange->value.value));

		if ((platform_decl.render_api >= RENDERAPI_OPENGL) &&
		        (platform_decl.render_api <= RENDERAPI_OPENGL4)) {
			render_vertexshader_t* vshader;
			render_pixelshader_t* pshader;

			GLuint handle = 0;
			size_t log_capacity = 2048;
			GLchar* log_buffer = memory_allocate(HASH_RESOURCE, log_capacity, 0, MEMORY_TEMPORARY);
			GLint log_length = (GLint)log_capacity;
			GLint compiled = 0;

			vshader = render_vertexshader_load(backend, vertexshader);
			pshader = render_pixelshader_load(backend, pixelshader);
			do {
				char name[256];
				GLint attributes = 0;
				GLint uniforms = 0;
				uint16_t offset = 0;

				handle = glCreateProgram();

				glAttachShader(handle, (GLuint)vshader->backend_data[0]);
				glAttachShader(handle, (GLuint)pshader->backend_data[0]);
				glLinkProgram(handle);
				glGetProgramiv(handle, GL_LINK_STATUS, &compiled);
				glGetProgramInfoLog(handle, log_length, &log_length, log_buffer);

				if (!compiled) {
					log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Unable to link program: %.*s"),
					           (int)log_length, log_buffer);
					result = -1;
					break;
				}
				else {
					if (log_length < 2) {
						string_t nomsg = string_copy(log_buffer, log_capacity, STRING_CONST("<no message>"));
						log_length = (GLint)nomsg.length;
					}
					log_debugf(HASH_RESOURCE, STRING_CONST("Successfully linked program: %.*s"), (int)log_length,
					           log_buffer);
				}

				glGetProgramiv(handle, GL_ACTIVE_ATTRIBUTES, &attributes);
				for (GLint ia = 0; ia < attributes; ++ia) {
					GLsizei num_chars = 0;
					GLint size = 0;
					GLenum type = GL_NONE;
					glGetActiveAttrib(handle, ia, sizeof(name), &num_chars, &size, &type, name);

					if (num_chars) {
						GLuint binding = 0;
						//TODO: Implement something proper, and generalized, together with dx10 input layout
						if (string_equal(name, num_chars, STRING_CONST("position")))
							binding = VERTEXATTRIBUTE_POSITION;
						else if (string_equal(name, num_chars, STRING_CONST("color")))
							binding = VERTEXATTRIBUTE_PRIMARYCOLOR;
						else if (string_equal(name, num_chars, STRING_CONST("texcoord")))
							binding = VERTEXATTRIBUTE_TEXCOORD0;
						glBindAttribLocation(handle, binding, name);
					}
				}

				glLinkProgram(handle);
				glGetProgramiv(handle, GL_LINK_STATUS, &compiled);
				glGetProgramInfoLog(handle, log_length, &log_length, log_buffer);

				if (!compiled) {
					log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Unable to relink program: %.*s"),
					           (int)log_length, log_buffer);
					result = -1;
					break;
				}
				else {
					if (log_length < 2) {
						string_t nomsg = string_copy(log_buffer, log_capacity, STRING_CONST("<no message>"));
						log_length = (GLint)nomsg.length;
					}
					log_debugf(HASH_RESOURCE, STRING_CONST("Successfully relinked program: %.*s"), (int)log_length,
					           log_buffer);
				}

				glGetProgramiv(handle, GL_ACTIVE_UNIFORMS, &uniforms);

				program = render_program_allocate(uniforms);

				for (GLint ia = 0; ia < attributes; ++ia) {
					GLsizei num_chars = 0;
					GLint size = 0;
					GLenum type = GL_NONE;
					render_vertex_attribute_t* attribute = program->attributes.attribute + ia;

					glGetActiveAttrib(handle, ia, sizeof(name), &num_chars, &size, &type, name);

					//TODO: Implement something proper, and generalized, together with dx10 input layout
					if (string_equal(name, num_chars, STRING_CONST("position")))
						attribute->binding = VERTEXATTRIBUTE_POSITION;
					else if (string_equal(name, num_chars, STRING_CONST("color")))
						attribute->binding = VERTEXATTRIBUTE_PRIMARYCOLOR;
					else if (string_equal(name, num_chars, STRING_CONST("texcoord")))
						attribute->binding = VERTEXATTRIBUTE_TEXCOORD0;
					else {
						log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL,
						           STRING_CONST("Invalid/unknown attribute name: %.*s"),
						           (int)num_chars, name);
						result = -1;
						break;
					}

					switch (type) {
					case GL_FLOAT:
						attribute->format = VERTEXFORMAT_FLOAT;
						break;
					case GL_FLOAT_VEC2:
						attribute->format = VERTEXFORMAT_FLOAT2;
						break;
					case GL_FLOAT_VEC3:
						attribute->format = VERTEXFORMAT_FLOAT3;
						break;
					case GL_FLOAT_VEC4:
						attribute->format = VERTEXFORMAT_FLOAT4;
						break;
					case GL_INT:
					case GL_UNSIGNED_INT:
						attribute->format = VERTEXFORMAT_INT;
						break;
					case GL_INT_VEC2:
					case GL_UNSIGNED_INT_VEC2:
						attribute->format = VERTEXFORMAT_INT2;
						break;
					case GL_INT_VEC4:
					case GL_UNSIGNED_INT_VEC4:
						attribute->format = VERTEXFORMAT_INT4;
						break;
					default:
						log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL,
						           STRING_CONST("Invalid/unknown attribute type: %u"),
						           (unsigned int)type);
						result = -1;
						break;
					}
					if (result < 0)
						break;

					++program->attributes.num_attributes;
				}

				//Sort and build attribute offsets
				for (size_t iend = program->attributes.num_attributes; iend > 1; --iend) {
					for (size_t ia = 0; ia < (iend - 1); ++ia) {
						render_vertex_attribute_t* first_attribute = program->attributes.attribute + ia;
						render_vertex_attribute_t* second_attribute = first_attribute + 1;
						if ((second_attribute->binding < first_attribute->binding)) {
							render_vertex_attribute_t tmp;
							tmp = *first_attribute;
							*first_attribute = *second_attribute;
							*second_attribute = tmp;
						}
					}
				}

				offset = 0;
				for (size_t ia = 0; ia < program->attributes.num_attributes; ++ia) {
					program->attributes.attribute[ia].offset = offset;
					offset += render_vertex_attribute_size(program->attributes.attribute[ia].format);
				}
				program->attributes.size = offset;

				offset = 0;
				program->parameters.num_parameters = 0;
				for (GLint iu = 0; iu < uniforms; ++iu) {
					GLsizei num_chars = 0;
					GLint size = 0;
					GLenum type = GL_NONE;
					render_parameter_t* parameter = program->parameters.parameters + iu;

					glGetActiveUniform(handle, iu, sizeof(name), &num_chars, &size, &type, name);

					parameter->name = hash(name, num_chars);
					parameter->location = glGetUniformLocation(handle, name);
					parameter->dim = size;
					parameter->offset = offset;
					parameter->stages = SHADER_VERTEX | SHADER_PIXEL;

					switch (type) {
					case GL_FLOAT_VEC4:
						parameter->type = RENDERPARAMETER_FLOAT4;
						offset += 16;
						break;
					case GL_INT_VEC4:
					case GL_UNSIGNED_INT_VEC4:
						parameter->type = RENDERPARAMETER_INT4;
						offset += 16;
						break;
					case GL_FLOAT_MAT4:
						parameter->type = RENDERPARAMETER_MATRIX;
						offset += 16 * 4;
						break;
					case GL_SAMPLER_2D:
						parameter->type = RENDERPARAMETER_TEXTURE;
						offset += 4;
						break;
					default:
						log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL,
						           STRING_CONST("Invalid/unknown uniform type: %u"),
						           (unsigned int)type);
						result = -1;
						break;
					}
					if (result < 0)
						break;

					++program->parameters.num_parameters;
				}
			}
			while (false);

			if (handle)
				glDeleteProgram(handle);

			render_pixelshader_deallocate(pshader);
			render_vertexshader_deallocate(vshader);

			memory_deallocate(log_buffer);
		}

		if (program) {
			stream = resource_local_create_static(uuid, subplatform);
			if (stream) {
				uint32_t version = 1;
				size_t uuid_size = sizeof(uuid_t) * 2;
				size_t program_size = sizeof(render_program_t) +
				                      sizeof(render_parameter_t) * program->parameters.num_parameters;
				stream_write_uint64(stream, resource_type_hash);
				stream_write_uint32(stream, version);
				stream_write_uint128(stream, vertexshader);
				stream_write_uint128(stream, pixelshader);
				stream_write(stream, pointer_offset(program, uuid_size), program_size - uuid_size);
				stream_deallocate(stream);
			}
			else {
				log_errorf(HASH_RESOURCE, ERROR_SYSTEM_CALL_FAIL,
				           STRING_CONST("Unable to compile program: Unable to create static resource stream"));
				result = -1;
			}
		}

		render_program_deallocate(program);

		render_backend_deallocate(backend);
		render_drawable_deallocate(drawable);
		window_deallocate(window);
	}

	return result;
}

#endif