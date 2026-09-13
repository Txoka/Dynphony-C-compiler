#include <stdlib.h>
#include <dynphony.h>
#include "dynphony/compiler.h"
#include "dynphony/frontend.h"
#include "dynphony/middle.h"
#include "dynphony/preprocessor.h"
#include "dynphony/target.h"

struct DynProjectReader {
    unsigned int address;
    unsigned int length;
    unsigned int position;
    int error;
};

static unsigned int dyn_output_length;
static unsigned int dyn_error_position;

static void dyn_copy_persistent(
    char *destination,
    unsigned int address,
    unsigned int length
);

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

static int dyn_project_read_text(
    struct DynProjectReader *reader,
    struct DynProjectText *text
) {
    unsigned int address;
    if (!dyn_project_skip_blob(reader, &address, &text->length)) return 0;
    text->data = malloc(text->length + 1u);
    if (!text->data) {
        reader->error = 1;
        return 0;
    }
    dyn_copy_persistent(text->data, address, text->length);
    text->data[text->length] = 0;
    return 1;
}

static void dyn_free_project_texts(
    struct DynProjectFile *files,
    unsigned int file_count,
    struct DynProjectText *roots,
    unsigned int root_count,
    struct DynProjectText *definitions,
    unsigned int definition_count
) {
    unsigned int index = 0;
    while (index < file_count) {
        if (files[index].path.data) free(files[index].path.data);
        if (files[index].contents.data) free(files[index].contents.data);
        index += 1u;
    }
    index = 0;
    while (index < root_count) {
        if (roots[index].data) free(roots[index].data);
        index += 1u;
    }
    index = 0;
    while (index < definition_count) {
        if (definitions[index].data) free(definitions[index].data);
        index += 1u;
    }
    if (files) free(files);
    if (roots) free(roots);
    if (definitions) free(definitions);
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
    if (position < length)
        word = ((unsigned int)source[position]) & 255u;
    word <<= 8;
    if (position + 1u < length)
        word |= ((unsigned int)source[position + 1u]) & 255u;
    word <<= 8;
    if (position + 2u < length)
        word |= ((unsigned int)source[position + 2u]) & 255u;
    word <<= 8;
    if (position + 3u < length)
        word |= ((unsigned int)source[position + 3u]) & 255u;
    return word;
}

static void dyn_write_failure(
    unsigned int output_address,
    unsigned int output_capacity
) {
    dyn_output_length = 8u;
    if (output_capacity >= 4u) persistent_store(output_address, 0u);
    if (output_capacity >= 8u)
        persistent_store(output_address + 4u, dyn_error_position);
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
    unsigned int node_capacity;
    unsigned int symbol_capacity;
    unsigned int dimension_capacity;
    unsigned int token_count = 0u;
    unsigned int name_count = 0u;
    int status = DYN_COMPILE_OK;

    *output_length = 0;
    dyn_error_position = 0xffffffffu;
    if (length > 1048576u) return DYN_COMPILE_INPUT_TOO_LARGE;
    {
        struct DynLexer lexer;
        dyn_lexer_init(&lexer, source, length);
        while (lexer.current.kind != DYN_TOK_EOF
            && lexer.current.kind != DYN_TOK_INVALID) {
            if (lexer.current.kind == DYN_TOK_IDENTIFIER
                || lexer.current.kind == DYN_TOK_MAIN
                || lexer.current.kind == DYN_TOK_STRING) name_count += 1u;
            token_count += 1u;
            dyn_lexer_next(&lexer);
        }
    }
    if (token_count > 0x7fffff00u) return DYN_COMPILE_INPUT_TOO_LARGE;
    node_capacity = token_count * 2u + 64u;
    symbol_capacity = name_count + 128u;
    dimension_capacity = token_count / 4u + 64u;
    program.nodes = malloc(node_capacity * sizeof(struct DynNode));
    if (!program.nodes) return DYN_COMPILE_OUT_OF_MEMORY;
    program.locals = malloc(symbol_capacity * sizeof(struct DynLocal));
    if (!program.locals) {
        free(program.nodes);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    program.functions = malloc(symbol_capacity * sizeof(struct DynFunction));
    if (!program.functions) {
        free(program.locals);
        free(program.nodes);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    program.globals = malloc(symbol_capacity * sizeof(struct DynGlobal));
    if (!program.globals) {
        free(program.functions);
        free(program.locals);
        free(program.nodes);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    program.constants = malloc(symbol_capacity * sizeof(struct DynConstant));
    if (!program.constants) {
        free(program.globals);
        free(program.functions);
        free(program.locals);
        free(program.nodes);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    program.aliases = malloc(symbol_capacity * sizeof(struct DynTypeAlias));
    program.enums = malloc(symbol_capacity * sizeof(struct DynEnumTag));
    if (!program.aliases || !program.enums) {
        if (program.enums) free(program.enums);
        if (program.aliases) free(program.aliases);
        free(program.constants);
        free(program.globals);
        free(program.functions);
        free(program.locals);
        free(program.nodes);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    program.structs = malloc(symbol_capacity * sizeof(struct DynStruct));
    program.members = malloc(symbol_capacity * sizeof(struct DynMember));
    program.dimensions = malloc(
        dimension_capacity * sizeof(struct DynDimension)
    );
    if (!program.structs || !program.members || !program.dimensions) {
        if (program.dimensions) free(program.dimensions);
        if (program.members) free(program.members);
        if (program.structs) free(program.structs);
        free(program.enums);
        free(program.aliases);
        free(program.constants);
        free(program.globals);
        free(program.functions);
        free(program.locals);
        free(program.nodes);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
    program.capacity = node_capacity;
    program.local_capacity = symbol_capacity;
    program.function_capacity = symbol_capacity;
    program.global_capacity = symbol_capacity;
    program.constant_capacity = symbol_capacity;
    program.alias_capacity = symbol_capacity;
    program.enum_capacity = symbol_capacity;
    program.struct_capacity = symbol_capacity;
    program.member_capacity = symbol_capacity;
    program.dimension_capacity = dimension_capacity;

    if (!dyn_parse(source, length, &program)) {
        dyn_error_position = program.error_position;
        status = DYN_COMPILE_PARSE_ERROR;
    } else if (!dyn_lower(&program, &module))
        status = DYN_COMPILE_SEMANTIC_ERROR;
    else {
        dyn_optimize(&module);
        if (!dyn_emit_image(
            &module, load_address, output, output_capacity, output_length
        )) status = DYN_COMPILE_OUTPUT_TOO_SMALL;
    }
    {
        unsigned int global_index = 0;
        while (global_index < program.global_count) {
            if (program.globals[global_index].data)
                free(program.globals[global_index].data);
            global_index += 1u;
        }
    }
    free(program.dimensions);
    free(program.members);
    free(program.structs);
    free(program.enums);
    free(program.aliases);
    free(program.constants);
    free(program.globals);
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
    unsigned int source_length;
    unsigned int source_count = 0;
    unsigned int image_length = 0;
    unsigned int record_length;
    unsigned int image_capacity;
    char *source;
    char *image;
    struct DynProjectFile *files = 0;
    struct DynProjectText *roots = 0;
    struct DynProjectText *definitions = 0;
    int status;

    dyn_output_length = 0;
    dyn_error_position = 0xffffffffu;
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
    if (file_count) files = calloc(file_count, sizeof(struct DynProjectFile));
    if (root_count) roots = calloc(root_count, sizeof(struct DynProjectText));
    if (definition_count) definitions = calloc(
        definition_count, sizeof(struct DynProjectText)
    );
    if ((file_count && !files) || (root_count && !roots)
        || (definition_count && !definitions)) reader.error = 1;
    index = 0;
    while (index < root_count && !reader.error) {
        dyn_project_read_text(&reader, &roots[index]);
        index += 1u;
    }
    index = 0;
    while (index < definition_count && !reader.error) {
        dyn_project_read_text(&reader, &definitions[index]);
        index += 1u;
    }
    index = 0;
    while (index < file_count && !reader.error) {
        unsigned int kind = dyn_project_u32(&reader);
        files[index].kind = kind;
        if (kind != 1u && kind != 2u) reader.error = 1;
        dyn_project_read_text(&reader, &files[index].path);
        dyn_project_read_text(&reader, &files[index].contents);
        if (kind == 1u) source_count += 1u;
        index += 1u;
    }
    if (
        reader.error || reader.position != reader.length || !source_count
    ) {
        dyn_free_project_texts(
            files, file_count, roots, root_count,
            definitions, definition_count
        );
        dyn_write_failure(output_address, output_capacity);
        return DYN_COMPILE_INVALID_PROJECT;
    }
    if (!dyn_preprocess_project(
        files, file_count, roots, root_count, definitions, definition_count,
        &source, &source_length
    )) {
        dyn_free_project_texts(
            files, file_count, roots, root_count,
            definitions, definition_count
        );
        dyn_write_failure(output_address, output_capacity);
        return DYN_COMPILE_PARSE_ERROR;
    }
    image_capacity = source_length * 8u + 4096u;
    if (output_capacity > 4u && image_capacity > output_capacity - 4u)
        image_capacity = output_capacity - 4u;
    image = malloc(image_capacity);
    if (!image) {
        free(source);
        dyn_free_project_texts(
            files, file_count, roots, root_count,
            definitions, definition_count
        );
        dyn_write_failure(output_address, output_capacity);
        return DYN_COMPILE_OUT_OF_MEMORY;
    }
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
    dyn_free_project_texts(
        files, file_count, roots, root_count, definitions, definition_count
    );
    return status;
}
