#ifndef DYNPHONY_PREPROCESSOR_H
#define DYNPHONY_PREPROCESSOR_H

struct DynProjectText {
    char *data;
    unsigned int length;
};

struct DynProjectFile {
    unsigned int kind;
    struct DynProjectText path;
    struct DynProjectText contents;
};

int dyn_preprocess_project(
    const struct DynProjectFile *files,
    unsigned int file_count,
    const struct DynProjectText *include_roots,
    unsigned int include_root_count,
    const struct DynProjectText *definitions,
    unsigned int definition_count,
    char **output,
    unsigned int *output_length
);

#endif
