#include <stdlib.h>
#include <dynphony.h>
#include "dynphony/compiler.h"
#include "dynphony/frontend.h"
#include "dynphony/middle.h"
#include "dynphony/target.h"

struct DynProjectReader {
    unsigned int address;
    unsigned int length;
    unsigned int position;
    int error;
};

static unsigned int dyn_output_length;

static unsigned int dyn_project_u32(struct DynProjectReader *reader) {
    unsigned int value;
    if (
        reader->position > reader->length
        || reader->length - reader->position < 4u
    ) {
        reader->error = 1;
        return 0;
    }
    value = persistent_load(reader->address + reader->position);
    reader->position += 4u;
    return value;
}

static int dyn_project_skip_blob(
    struct DynProjectReader *reader,
    unsigned int *address,
    unsigned int *length
) {
    unsigned int size = dyn_project_u32(reader);
    unsigned int padded;
    if (reader->error || size > 0xfffffffcu) return 0;
    padded = (size + 3u) & 0xfffffffcu;
    if (
        reader->position > reader->length
        || padded > reader->length - reader->position
    ) {
        reader->error = 1;
        return 0;
    }
    *address = reader->address + reader->position;
    *length = size;
    reader->position += padded;
    return 1;
}

static void dyn_copy_persistent(
    char *destination,
    unsigned int address,
    unsigned int length
) {
    unsigned int index = 0;
    while (index < length) {
        unsigned int word = persistent_load(address + (index & 0xfffffffcu));
        unsigned int shift = 24u - ((index & 3u) << 3);
        destination[index] = (char)((word >> shift) & 255u);
        index += 1u;
    }
}

static unsigned int dyn_pack_word(
    const char *source,
    unsigned int position,
    unsigned int length
) {
    unsigned int word = 0;
    unsigned int count = 0;
    while (count < 4u) {
        word <<= 8;
        if (position + count < length)
            word |= ((unsigned int)source[position + count]) & 255u;
        count += 1u;
    }
    return word;
}

static void dyn_write_failure(
    unsigned int output_address,
    unsigned int output_capacity
) {
    dyn_output_length = 8u;
    if (output_capacity >= 4u) persistent_store(output_address, 0u);
    if (output_capacity >= 8u) persistent_store(output_address + 4u, 0u);
}

unsigned int dyn_last_output_length(void) {
    return dyn_output_length;
}

int dyn_compile_buffer(
    const char *source,
    unsigned int length,
    unsigned int load_address,
    char *output,
    unsigned int output_capacity,
    unsigned int *output_length
) {
    struct DynAstProgram program;
    struct DynIrModule module;
    unsigned int capacity;
    int status = DYN_COMPILE_OK;

    *output_length = 0;
    if (length > 1048576u) return DYN_COMPILE_INPUT_TOO_LARGE;
    capacity = length + 1u;
    program.nodes = calloc(capacity, sizeof(struct DynNode));
    if (!program.nodes) return DYN_COMPILE_OUT_OF_MEMORY;
    program.locals = calloc(capacity, sizeof(struct DynLocal));
    if (!program.locals) {
        free(program.nodes);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    program.functions = calloc(capacity, sizeof(struct DynFunction));
    if (!program.functions) {
        free(program.locals);
        free(program.nodes);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    program.capacity = capacity;
    program.local_capacity = capacity;
    program.function_capacity = capacity;

    if (!dyn_parse(source, length, &program))
        status = DYN_COMPILE_PARSE_ERROR;
    else if (!dyn_lower(&program, &module))
        status = DYN_COMPILE_SEMANTIC_ERROR;
    else {
        dyn_optimize(&module);
        if (!dyn_emit_image(
            &module, load_address, output, output_capacity, output_length
        )) status = DYN_COMPILE_OUTPUT_TOO_SMALL;
    }
    free(program.functions);
    free(program.locals);
    free(program.nodes);
    return status;
}

int dyn_compile_project(
    unsigned int project_address,
    unsigned int project_byte_length,
    unsigned int program_load_address,
    unsigned int output_address,
    unsigned int output_capacity
) {
    struct DynProjectReader reader;
    unsigned int file_count;
    unsigned int root_count;
    unsigned int definition_count;
    unsigned int index;
    unsigned int unused_address;
    unsigned int unused_length;
    unsigned int source_address = 0;
    unsigned int source_length = 0;
    unsigned int source_count = 0;
    unsigned int image_length = 0;
    unsigned int record_length;
    unsigned int image_capacity;
    char *source;
    char *image;
    int status;

    dyn_output_length = 0;
    reader.address = project_address;
    reader.length = project_byte_length;
    reader.position = 0;
    reader.error = 0;
    if (dyn_project_u32(&reader) != 0x44435031u) reader.error = 1;
    if (dyn_project_u32(&reader) != 1u) reader.error = 1;
    if (dyn_project_u32(&reader) != 0u) reader.error = 1;
    file_count = dyn_project_u32(&reader);
    root_count = dyn_project_u32(&reader);
    definition_count = dyn_project_u32(&reader);
    index = 0;
    while (index < root_count + definition_count && !reader.error) {
        dyn_project_skip_blob(&reader, &unused_address, &unused_length);
        index += 1u;
    }
    index = 0;
    while (index < file_count && !reader.error) {
        unsigned int kind = dyn_project_u32(&reader);
        unsigned int contents_address;
        unsigned int contents_length;
        dyn_project_skip_blob(&reader, &unused_address, &unused_length);
        dyn_project_skip_blob(&reader, &contents_address, &contents_length);
        if (kind == 1u) {
            source_count += 1u;
            source_address = contents_address;
            source_length = contents_length;
        } else if (kind != 2u) reader.error = 1;
        index += 1u;
    }
    if (
        reader.error || reader.position != reader.length || source_count != 1u
    ) {
        dyn_write_failure(output_address, output_capacity);
        return DYN_COMPILE_INVALID_PROJECT;
    }
    source = malloc(source_length + 1u);
    if (!source) {
        dyn_write_failure(output_address, output_capacity);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    image_capacity = source_length * 128u + 4096u;
    if (output_capacity > 4u && image_capacity > output_capacity - 4u)
        image_capacity = output_capacity - 4u;
    image = malloc(image_capacity);
    if (!image) {
        free(source);
        dyn_write_failure(output_address, output_capacity);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    dyn_copy_persistent(source, source_address, source_length);
    source[source_length] = 0;
    status = dyn_compile_buffer(
        source, source_length, program_load_address, image,
        image_capacity, &image_length
    );
    record_length = 4u + ((image_length + 3u) & 0xfffffffcu);
    dyn_output_length = record_length;
    if (status == DYN_COMPILE_OK && output_capacity < record_length)
        status = DYN_COMPILE_OUTPUT_TOO_SMALL;
    if (status == DYN_COMPILE_OK) {
        persistent_store(output_address, image_length);
        index = 0;
        while (index < image_length) {
            persistent_store(
                output_address + 4u + index,
                dyn_pack_word(image, index, image_length)
            );
            index += 4u;
        }
    } else dyn_write_failure(output_address, output_capacity);
    free(image);
    free(source);
    return status;
}
