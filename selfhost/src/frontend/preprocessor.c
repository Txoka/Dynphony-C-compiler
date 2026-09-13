#include <stdlib.h>
#include "dynphony/preprocessor.h"

enum { DYN_PP_MAX_DEPTH = 64u, DYN_PP_MAX_PARAMETERS = 8u };

struct DynMacro {
    struct DynProjectText name;
    struct DynProjectText replacement;
    struct DynProjectText parameters[DYN_PP_MAX_PARAMETERS];
    unsigned int parameter_count;
    int function_like;
};

struct DynPreprocessor {
    const struct DynProjectFile *files;
    unsigned int file_count;
    const struct DynProjectText *roots;
    unsigned int root_count;
    struct DynMacro *macros;
    unsigned int macro_count;
    unsigned int macro_capacity;
    char *once_files;
    char *active_files;
    char *output;
    unsigned int length;
    unsigned int capacity;
    int error;
};

static int dyn_preprocessor_space(char value) {
    return value == ' ' || value == '\t' || value == '\r';
}

static int dyn_name_char(char value) {
    return (value >= 'a' && value <= 'z')
        || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9') || value == '_';
}

static int dyn_text_equal(
    const char *left, unsigned int left_length,
    const char *right, unsigned int right_length
) {
    unsigned int index = 0;
    if (left_length != right_length) return 0;
    while (index < left_length) {
        if (left[index] != right[index]) return 0;
        index += 1u;
    }
    return 1;
}

static int dyn_word(
    const char *text, unsigned int length, const char *word,
    unsigned int word_length
) {
    return dyn_text_equal(text, length, word, word_length);
}

static unsigned int dyn_find_macro(
    const struct DynPreprocessor *preprocessor,
    const char *name, unsigned int length
) {
    unsigned int index = 0;
    while (index < preprocessor->macro_count) {
        if (dyn_text_equal(
            name, length, preprocessor->macros[index].name.data,
            preprocessor->macros[index].name.length
        )) return index;
        index += 1u;
    }
    return 0xffffffffu;
}

static int dyn_macro_defined(
    const struct DynPreprocessor *preprocessor,
    const char *name, unsigned int length
) {
    return dyn_find_macro(preprocessor, name, length) != 0xffffffffu;
}

struct DynCondition {
    const struct DynPreprocessor *preprocessor;
    const char *text;
    unsigned int length;
    unsigned int position;
    unsigned int depth;
    int error;
};

static void dyn_condition_skip(struct DynCondition *condition) {
    while (condition->position < condition->length
        && dyn_preprocessor_space(condition->text[condition->position]))
        condition->position += 1u;
}

static int dyn_condition_take(
    struct DynCondition *condition,
    const char *operator_text,
    unsigned int operator_length
) {
    dyn_condition_skip(condition);
    if (condition->position + operator_length > condition->length
        || !dyn_text_equal(
            condition->text + condition->position, operator_length,
            operator_text, operator_length
        )) return 0;
    condition->position += operator_length;
    return 1;
}

static unsigned int dyn_condition_or(struct DynCondition *condition);

static unsigned int dyn_condition_primary(struct DynCondition *condition) {
    unsigned int value = 0u;
    unsigned int start;
    unsigned int macro;
    dyn_condition_skip(condition);
    if (dyn_condition_take(condition, "(", 1u)) {
        value = dyn_condition_or(condition);
        if (!dyn_condition_take(condition, ")", 1u)) condition->error = 1;
        return value;
    }
    start = condition->position;
    if (start < condition->length
        && condition->text[start] >= '0' && condition->text[start] <= '9') {
        unsigned int base = 10u;
        if (condition->text[start] == '0') {
            base = 8u;
            condition->position += 1u;
            if (condition->position < condition->length
                && (condition->text[condition->position] == 'x'
                    || condition->text[condition->position] == 'X')) {
                base = 16u;
                condition->position += 1u;
            }
        }
        while (condition->position < condition->length) {
            char digit = condition->text[condition->position];
            unsigned int decoded;
            if (digit >= '0' && digit <= '9') decoded = (unsigned int)(digit - '0');
            else if (digit >= 'a' && digit <= 'f') decoded = (unsigned int)(digit - 'a') + 10u;
            else if (digit >= 'A' && digit <= 'F') decoded = (unsigned int)(digit - 'A') + 10u;
            else break;
            if (decoded >= base) break;
            value = value * base + decoded;
            condition->position += 1u;
        }
        while (condition->position < condition->length
            && (condition->text[condition->position] == 'u'
                || condition->text[condition->position] == 'U'
                || condition->text[condition->position] == 'l'
                || condition->text[condition->position] == 'L'))
            condition->position += 1u;
        return value;
    }
    if (start < condition->length
        && ((condition->text[start] >= 'a' && condition->text[start] <= 'z')
            || (condition->text[start] >= 'A' && condition->text[start] <= 'Z')
            || condition->text[start] == '_')) {
        while (condition->position < condition->length
            && dyn_name_char(condition->text[condition->position]))
            condition->position += 1u;
        if (dyn_word(
            condition->text + start, condition->position - start,
            "defined", 7u
        )) {
            unsigned int name_start;
            unsigned int name_end;
            int parenthesized = dyn_condition_take(condition, "(", 1u);
            dyn_condition_skip(condition);
            name_start = condition->position;
            while (condition->position < condition->length
                && dyn_name_char(condition->text[condition->position]))
                condition->position += 1u;
            name_end = condition->position;
            if (name_start == name_end || (parenthesized
                && !dyn_condition_take(condition, ")", 1u))) {
                condition->error = 1;
                return 0u;
            }
            return dyn_macro_defined(
                condition->preprocessor,
                condition->text + name_start, name_end - name_start
            );
        }
        macro = dyn_find_macro(
            condition->preprocessor, condition->text + start,
            condition->position - start
        );
        if (macro != 0xffffffffu
            && !condition->preprocessor->macros[macro].function_like
            && condition->depth < DYN_PP_MAX_DEPTH) {
            struct DynCondition nested;
            nested.preprocessor = condition->preprocessor;
            nested.text = condition->preprocessor->macros[macro].replacement.data;
            nested.length = condition->preprocessor->macros[macro].replacement.length;
            nested.position = 0u;
            nested.depth = condition->depth + 1u;
            nested.error = 0;
            value = dyn_condition_or(&nested);
            dyn_condition_skip(&nested);
            if (nested.error || nested.position != nested.length)
                condition->error = 1;
            return value;
        }
        return 0u;
    }
    condition->error = 1;
    return 0u;
}

static unsigned int dyn_condition_unary(struct DynCondition *condition) {
    if (dyn_condition_take(condition, "!", 1u))
        return !dyn_condition_unary(condition);
    if (dyn_condition_take(condition, "~", 1u))
        return ~dyn_condition_unary(condition);
    if (dyn_condition_take(condition, "+", 1u))
        return dyn_condition_unary(condition);
    if (dyn_condition_take(condition, "-", 1u))
        return 0u - dyn_condition_unary(condition);
    return dyn_condition_primary(condition);
}

static unsigned int dyn_condition_multiply(struct DynCondition *condition) {
    unsigned int value = dyn_condition_unary(condition);
    while (!condition->error) {
        if (dyn_condition_take(condition, "*", 1u))
            value *= dyn_condition_unary(condition);
        else if (dyn_condition_take(condition, "/", 1u)) {
            unsigned int right = dyn_condition_unary(condition);
            if (!right) condition->error = 1; else value /= right;
        } else if (dyn_condition_take(condition, "%", 1u)) {
            unsigned int right = dyn_condition_unary(condition);
            if (!right) condition->error = 1; else value %= right;
        } else break;
    }
    return value;
}

static unsigned int dyn_condition_add(struct DynCondition *condition) {
    unsigned int value = dyn_condition_multiply(condition);
    while (!condition->error) {
        if (dyn_condition_take(condition, "+", 1u))
            value += dyn_condition_multiply(condition);
        else if (dyn_condition_take(condition, "-", 1u))
            value -= dyn_condition_multiply(condition);
        else break;
    }
    return value;
}

static unsigned int dyn_condition_shift(struct DynCondition *condition) {
    unsigned int value = dyn_condition_add(condition);
    while (!condition->error) {
        if (dyn_condition_take(condition, "<<", 2u))
            value <<= dyn_condition_add(condition) & 31u;
        else if (dyn_condition_take(condition, ">>", 2u))
            value >>= dyn_condition_add(condition) & 31u;
        else break;
    }
    return value;
}

static unsigned int dyn_condition_relation(struct DynCondition *condition) {
    unsigned int value = dyn_condition_shift(condition);
    while (!condition->error) {
        if (dyn_condition_take(condition, "<=", 2u)) value = value <= dyn_condition_shift(condition);
        else if (dyn_condition_take(condition, ">=", 2u)) value = value >= dyn_condition_shift(condition);
        else if (dyn_condition_take(condition, "<", 1u)) value = value < dyn_condition_shift(condition);
        else if (dyn_condition_take(condition, ">", 1u)) value = value > dyn_condition_shift(condition);
        else break;
    }
    return value;
}

static unsigned int dyn_condition_equal(struct DynCondition *condition) {
    unsigned int value = dyn_condition_relation(condition);
    while (!condition->error) {
        if (dyn_condition_take(condition, "==", 2u)) value = value == dyn_condition_relation(condition);
        else if (dyn_condition_take(condition, "!=", 2u)) value = value != dyn_condition_relation(condition);
        else break;
    }
    return value;
}

static unsigned int dyn_condition_bit_and(struct DynCondition *condition) {
    unsigned int value = dyn_condition_equal(condition);
    while (!condition->error) {
        unsigned int saved = condition->position;
        if (!dyn_condition_take(condition, "&", 1u)) break;
        if (dyn_condition_take(condition, "&", 1u)) {
            condition->position = saved;
            break;
        }
        value &= dyn_condition_equal(condition);
    }
    return value;
}

static unsigned int dyn_condition_bit_xor(struct DynCondition *condition) {
    unsigned int value = dyn_condition_bit_and(condition);
    while (dyn_condition_take(condition, "^", 1u))
        value ^= dyn_condition_bit_and(condition);
    return value;
}

static unsigned int dyn_condition_bit_or(struct DynCondition *condition) {
    unsigned int value = dyn_condition_bit_xor(condition);
    while (!condition->error) {
        unsigned int saved = condition->position;
        if (!dyn_condition_take(condition, "|", 1u)) break;
        if (dyn_condition_take(condition, "|", 1u)) {
            condition->position = saved;
            break;
        }
        value |= dyn_condition_bit_xor(condition);
    }
    return value;
}

static unsigned int dyn_condition_and(struct DynCondition *condition) {
    unsigned int value = dyn_condition_bit_or(condition);
    while (dyn_condition_take(condition, "&&", 2u)) {
        unsigned int right = dyn_condition_bit_or(condition);
        value = value && right;
    }
    return value;
}

static unsigned int dyn_condition_or(struct DynCondition *condition) {
    unsigned int value = dyn_condition_and(condition);
    while (dyn_condition_take(condition, "||", 2u)) {
        unsigned int right = dyn_condition_and(condition);
        value = value || right;
    }
    return value;
}

static int dyn_condition(
    const struct DynPreprocessor *preprocessor,
    const char *text,
    unsigned int length,
    int *valid
) {
    struct DynCondition condition;
    unsigned int value;
    condition.preprocessor = preprocessor;
    condition.text = text;
    condition.length = length;
    condition.position = 0u;
    condition.depth = 0u;
    condition.error = 0;
    value = dyn_condition_or(&condition);
    dyn_condition_skip(&condition);
    *valid = !condition.error && condition.position == condition.length;
    return value != 0u;
}

static void dyn_define(
    struct DynPreprocessor *preprocessor,
    const char *name, unsigned int length,
    const char *replacement, unsigned int replacement_length
) {
    unsigned int existing;
    if (!length) return;
    existing = dyn_find_macro(preprocessor, name, length);
    if (existing != 0xffffffffu) {
        preprocessor->macros[existing].replacement.data = (char *)replacement;
        preprocessor->macros[existing].replacement.length = replacement_length;
        preprocessor->macros[existing].parameter_count = 0u;
        preprocessor->macros[existing].function_like = 0;
        return;
    }
    if (preprocessor->macro_count >= preprocessor->macro_capacity) {
        preprocessor->error = 1;
        return;
    }
    preprocessor->macros[preprocessor->macro_count].name.data = (char *)name;
    preprocessor->macros[preprocessor->macro_count].name.length = length;
    preprocessor->macros[preprocessor->macro_count].replacement.data =
        (char *)replacement;
    preprocessor->macros[preprocessor->macro_count].replacement.length =
        replacement_length;
    preprocessor->macros[preprocessor->macro_count].parameter_count = 0u;
    preprocessor->macros[preprocessor->macro_count].function_like = 0;
    preprocessor->macro_count += 1u;
}

static void dyn_define_function(
    struct DynPreprocessor *preprocessor,
    const char *name,
    unsigned int name_length,
    const char *parameters,
    unsigned int parameters_length,
    const char *replacement,
    unsigned int replacement_length
) {
    unsigned int macro_index;
    unsigned int position = 0u;
    dyn_define(
        preprocessor, name, name_length, replacement, replacement_length
    );
    if (preprocessor->error) return;
    macro_index = dyn_find_macro(preprocessor, name, name_length);
    preprocessor->macros[macro_index].function_like = 1;
    while (position < parameters_length) {
        unsigned int start;
        while (position < parameters_length
            && dyn_preprocessor_space(parameters[position])) position += 1u;
        if (position >= parameters_length) break;
        start = position;
        while (position < parameters_length
            && dyn_name_char(parameters[position])) position += 1u;
        if (start == position || preprocessor->macros[macro_index].parameter_count
            >= DYN_PP_MAX_PARAMETERS) {
            preprocessor->error = 1;
            return;
        }
        preprocessor->macros[macro_index].parameters[
            preprocessor->macros[macro_index].parameter_count
        ].data = (char *)(parameters + start);
        preprocessor->macros[macro_index].parameters[
            preprocessor->macros[macro_index].parameter_count
        ].length = position - start;
        preprocessor->macros[macro_index].parameter_count += 1u;
        while (position < parameters_length
            && dyn_preprocessor_space(parameters[position])) position += 1u;
        if (position < parameters_length) {
            if (parameters[position] != ',') {
                preprocessor->error = 1;
                return;
            }
            position += 1u;
        }
    }
}

static void dyn_undef(
    struct DynPreprocessor *preprocessor,
    const char *name,
    unsigned int length
) {
    unsigned int index = dyn_find_macro(preprocessor, name, length);
    if (index == 0xffffffffu) return;
    while (index + 1u < preprocessor->macro_count) {
        preprocessor->macros[index].name.data =
            preprocessor->macros[index + 1u].name.data;
        preprocessor->macros[index].name.length =
            preprocessor->macros[index + 1u].name.length;
        preprocessor->macros[index].replacement.data =
            preprocessor->macros[index + 1u].replacement.data;
        preprocessor->macros[index].replacement.length =
            preprocessor->macros[index + 1u].replacement.length;
        preprocessor->macros[index].parameter_count =
            preprocessor->macros[index + 1u].parameter_count;
        preprocessor->macros[index].function_like =
            preprocessor->macros[index + 1u].function_like;
        {
            unsigned int parameter = 0u;
            while (parameter < DYN_PP_MAX_PARAMETERS) {
                preprocessor->macros[index].parameters[parameter].data =
                    preprocessor->macros[index + 1u].parameters[parameter].data;
                preprocessor->macros[index].parameters[parameter].length =
                    preprocessor->macros[index + 1u].parameters[parameter].length;
                parameter += 1u;
            }
        }
        index += 1u;
    }
    preprocessor->macro_count -= 1u;
}

static void dyn_expand_function(
    struct DynPreprocessor *preprocessor,
    const struct DynMacro *macro,
    const char *source,
    unsigned int *position,
    unsigned int end
) {
    struct DynProjectText arguments[DYN_PP_MAX_PARAMETERS];
    unsigned int count = 0u;
    unsigned int cursor = *position;
    unsigned int depth = 0u;
    unsigned int argument_start;
    unsigned int replacement_position = 0u;
    char quoted = 0;
    while (cursor < end && dyn_preprocessor_space(source[cursor])) cursor += 1u;
    if (cursor >= end || source[cursor] != '(') return;
    cursor += 1u;
    argument_start = cursor;
    while (cursor < end) {
        if (source[cursor] == '(') depth += 1u;
        else if (source[cursor] == ')' && depth) depth -= 1u;
        else if ((source[cursor] == ',' && !depth)
            || (source[cursor] == ')' && !depth)) {
            if (count >= DYN_PP_MAX_PARAMETERS) {
                preprocessor->error = 1;
                return;
            }
            arguments[count].data = (char *)(source + argument_start);
            arguments[count].length = cursor - argument_start;
            count += 1u;
            if (source[cursor] == ')') break;
            argument_start = cursor + 1u;
        }
        cursor += 1u;
    }
    if (cursor >= end) {
        preprocessor->error = 1;
        return;
    }
    if (count == 1u && !arguments[0].length && !macro->parameter_count)
        count = 0u;
    if (count != macro->parameter_count) {
        preprocessor->error = 1;
        return;
    }
    while (replacement_position < macro->replacement.length) {
        if (quoted) {
            char value = macro->replacement.data[replacement_position];
            dyn_append(
                preprocessor, macro->replacement.data + replacement_position,
                1u
            );
            replacement_position += 1u;
            if (value == '\\' && replacement_position < macro->replacement.length) {
                dyn_append(
                    preprocessor,
                    macro->replacement.data + replacement_position, 1u
                );
                replacement_position += 1u;
            } else if (value == quoted) quoted = 0;
        } else if (macro->replacement.data[replacement_position] == '"'
            || macro->replacement.data[replacement_position] == '\'') {
            quoted = macro->replacement.data[replacement_position];
            dyn_append(
                preprocessor, macro->replacement.data + replacement_position,
                1u
            );
            replacement_position += 1u;
        } else if ((macro->replacement.data[replacement_position] >= 'a'
                && macro->replacement.data[replacement_position] <= 'z')
            || (macro->replacement.data[replacement_position] >= 'A'
                && macro->replacement.data[replacement_position] <= 'Z')
            || macro->replacement.data[replacement_position] == '_') {
            unsigned int start = replacement_position;
            unsigned int parameter = 0u;
            while (replacement_position < macro->replacement.length
                && dyn_name_char(macro->replacement.data[replacement_position]))
                replacement_position += 1u;
            while (parameter < macro->parameter_count && !dyn_text_equal(
                macro->replacement.data + start, replacement_position - start,
                macro->parameters[parameter].data,
                macro->parameters[parameter].length
            )) parameter += 1u;
            if (parameter < macro->parameter_count)
                dyn_append(
                    preprocessor, arguments[parameter].data,
                    arguments[parameter].length
                );
            else dyn_append(
                preprocessor, macro->replacement.data + start,
                replacement_position - start
            );
        } else {
            dyn_append(
                preprocessor, macro->replacement.data + replacement_position,
                1u
            );
            replacement_position += 1u;
        }
    }
    *position = cursor + 1u;
}

static void dyn_append(
    struct DynPreprocessor *preprocessor,
    const char *text, unsigned int length
) {
    unsigned int index = 0;
    if (length > preprocessor->capacity - preprocessor->length) {
        preprocessor->error = 1;
        return;
    }
    while (index < length) {
        preprocessor->output[preprocessor->length] = text[index];
        preprocessor->length += 1u;
        index += 1u;
    }
}

static void dyn_expand_line(
    struct DynPreprocessor *preprocessor,
    const char *source,
    unsigned int start,
    unsigned int end
) {
    unsigned int position = start;
    char quoted = 0;
    while (position < end && !preprocessor->error) {
        if (quoted) {
            char value = source[position];
            dyn_append(preprocessor, source + position, 1u);
            position += 1u;
            if (value == '\\' && position < end) {
                dyn_append(preprocessor, source + position, 1u);
                position += 1u;
            } else if (value == quoted) quoted = 0;
        } else if (source[position] == '"' || source[position] == '\'') {
            quoted = source[position];
            dyn_append(preprocessor, source + position, 1u);
            position += 1u;
        } else if ((source[position] >= 'a' && source[position] <= 'z')
            || (source[position] >= 'A' && source[position] <= 'Z')
            || source[position] == '_') {
            unsigned int name_start = position;
            unsigned int macro;
            while (position < end && dyn_name_char(source[position]))
                position += 1u;
            macro = dyn_find_macro(
                preprocessor, source + name_start, position - name_start
            );
            if (macro == 0xffffffffu)
                dyn_append(
                    preprocessor, source + name_start, position - name_start
                );
            else if (preprocessor->macros[macro].function_like) {
                unsigned int before = position;
                dyn_expand_function(
                    preprocessor, &preprocessor->macros[macro], source,
                    &position, end
                );
                if (position == before) dyn_append(
                    preprocessor, source + name_start, position - name_start
                );
            } else dyn_append(
                preprocessor, preprocessor->macros[macro].replacement.data,
                preprocessor->macros[macro].replacement.length
            );
        } else {
            dyn_append(preprocessor, source + position, 1u);
            position += 1u;
        }
    }
}

static int dyn_path_matches(
    const struct DynProjectText *path,
    const char *prefix, unsigned int prefix_length,
    const char *name, unsigned int name_length
) {
    unsigned int index = 0;
    if (path->length != prefix_length + name_length) return 0;
    while (index < prefix_length) {
        if (path->data[index] != prefix[index]) return 0;
        index += 1u;
    }
    index = 0;
    while (index < name_length) {
        if (path->data[prefix_length + index] != name[index]) return 0;
        index += 1u;
    }
    return 1;
}

static unsigned int dyn_find_include(
    const struct DynPreprocessor *preprocessor,
    unsigned int current_file,
    const char *name,
    unsigned int name_length,
    int quoted
) {
    unsigned int file;
    if (quoted) {
        const struct DynProjectText *path =
            &preprocessor->files[current_file].path;
        unsigned int prefix_length = path->length;
        while (prefix_length && path->data[prefix_length - 1u] != '/')
            prefix_length -= 1u;
        file = 0;
        while (file < preprocessor->file_count) {
            if (dyn_path_matches(
                &preprocessor->files[file].path,
                path->data, prefix_length, name, name_length
            )) return file;
            file += 1u;
        }
    }
    {
        unsigned int root = 0;
        while (root < preprocessor->root_count) {
            const struct DynProjectText *prefix = &preprocessor->roots[root];
            file = 0;
            while (file < preprocessor->file_count) {
                const struct DynProjectText *path =
                    &preprocessor->files[file].path;
                if (path->length == prefix->length + 1u + name_length
                    && dyn_text_equal(
                        path->data, prefix->length,
                        prefix->data, prefix->length
                    ) && path->data[prefix->length] == '/') {
                    unsigned int offset = prefix->length + 1u;
                    if (dyn_text_equal(
                        path->data + offset, name_length, name, name_length
                    )) return file;
                }
                file += 1u;
            }
            root += 1u;
        }
    }
    file = 0;
    while (file < preprocessor->file_count) {
        if (dyn_path_matches(
            &preprocessor->files[file].path, "", 0u, name, name_length
        )) return file;
        file += 1u;
    }
    return 0xffffffffu;
}

static void dyn_process_file(
    struct DynPreprocessor *preprocessor,
    unsigned int file,
    unsigned int depth
) {
    const char *source;
    unsigned int source_length;
    unsigned int position = 0;
    int active = 1;
    int parent[DYN_PP_MAX_DEPTH];
    int taken[DYN_PP_MAX_DEPTH];
    int seen_else[DYN_PP_MAX_DEPTH];
    unsigned int conditional_depth = 0;
    if (depth >= DYN_PP_MAX_DEPTH || file >= preprocessor->file_count) {
        preprocessor->error = 1;
        return;
    }
    if (preprocessor->once_files[file]) return;
    if (preprocessor->active_files[file]) {
        preprocessor->error = 1;
        return;
    }
    preprocessor->active_files[file] = 1;
    source = preprocessor->files[file].contents.data;
    source_length = preprocessor->files[file].contents.length;
    while (position < source_length && !preprocessor->error) {
        unsigned int line_start = position;
        unsigned int line_end;
        unsigned int cursor;
        while (position < source_length && source[position] != '\n')
            position += 1u;
        line_end = position;
        if (position < source_length) position += 1u;
        cursor = line_start;
        while (cursor < line_end && dyn_preprocessor_space(source[cursor])) cursor += 1u;
        if (cursor < line_end && source[cursor] == '#') {
            unsigned int directive_start;
            unsigned int directive_length;
            unsigned int argument_start;
            cursor += 1u;
            while (cursor < line_end && dyn_preprocessor_space(source[cursor])) cursor += 1u;
            directive_start = cursor;
            while (cursor < line_end && dyn_name_char(source[cursor]))
                cursor += 1u;
            directive_length = cursor - directive_start;
            while (cursor < line_end && dyn_preprocessor_space(source[cursor])) cursor += 1u;
            argument_start = cursor;
            if (dyn_word(
                source + directive_start, directive_length, "ifndef", 6u
            ) || dyn_word(
                source + directive_start, directive_length, "ifdef", 5u
            ) || dyn_word(
                source + directive_start, directive_length, "if", 2u
            )) {
                int condition;
                int valid = 1;
                unsigned int argument_length = line_end - argument_start;
                if (conditional_depth >= DYN_PP_MAX_DEPTH) {
                    preprocessor->error = 1;
                    return;
                }
                parent[conditional_depth] = active;
                if (!active) condition = 0;
                else if (directive_length == 2u) condition = dyn_condition(
                    preprocessor, source + argument_start, argument_length,
                    &valid
                );
                else {
                    condition = dyn_macro_defined(
                        preprocessor, source + argument_start, argument_length
                    );
                    if (directive_length == 6u) condition = !condition;
                }
                if (!valid) preprocessor->error = 1;
                taken[conditional_depth] = condition;
                seen_else[conditional_depth] = 0;
                conditional_depth += 1u;
                active = active && condition;
            } else if (dyn_word(
                source + directive_start, directive_length, "elif", 4u
            )) {
                if (!conditional_depth) preprocessor->error = 1;
                else {
                    unsigned int state = conditional_depth - 1u;
                    int valid = 1;
                    int condition = 0;
                    if (seen_else[state]) preprocessor->error = 1;
                    else if (parent[state] && !taken[state]) condition =
                        dyn_condition(
                            preprocessor, source + argument_start,
                            line_end - argument_start, &valid
                        );
                    if (!valid) preprocessor->error = 1;
                    active = parent[state] && !taken[state] && condition;
                    taken[state] = taken[state] || condition;
                }
            } else if (dyn_word(
                source + directive_start, directive_length, "else", 4u
            )) {
                if (!conditional_depth) preprocessor->error = 1;
                else {
                    unsigned int state = conditional_depth - 1u;
                    if (seen_else[state]) preprocessor->error = 1;
                    active = parent[state] && !taken[state];
                    taken[state] = 1;
                    seen_else[state] = 1;
                }
            } else if (dyn_word(
                source + directive_start, directive_length, "endif", 5u
            )) {
                if (!conditional_depth) preprocessor->error = 1;
                else {
                    conditional_depth -= 1u;
                    active = parent[conditional_depth];
                }
            } else if (active && dyn_word(
                source + directive_start, directive_length, "define", 6u
            )) {
                unsigned int name_end = argument_start;
                unsigned int replacement_start;
                while (name_end < line_end && dyn_name_char(source[name_end]))
                    name_end += 1u;
                replacement_start = name_end;
                if (replacement_start < line_end
                    && source[replacement_start] == '(') {
                    unsigned int parameters_start = replacement_start + 1u;
                    unsigned int parameters_end = parameters_start;
                    while (parameters_end < line_end
                        && source[parameters_end] != ')') parameters_end += 1u;
                    if (parameters_end >= line_end) {
                        preprocessor->error = 1;
                        return;
                    }
                    replacement_start = parameters_end + 1u;
                    while (replacement_start < line_end
                        && dyn_preprocessor_space(source[replacement_start]))
                        replacement_start += 1u;
                    dyn_define_function(
                        preprocessor, source + argument_start,
                        name_end - argument_start,
                        source + parameters_start,
                        parameters_end - parameters_start,
                        source + replacement_start,
                        line_end - replacement_start
                    );
                } else {
                while (replacement_start < line_end
                    && dyn_preprocessor_space(source[replacement_start]))
                    replacement_start += 1u;
                dyn_define(
                    preprocessor, source + argument_start,
                    name_end - argument_start,
                    source + replacement_start, line_end - replacement_start
                );
                }
            } else if (active && dyn_word(
                source + directive_start, directive_length, "undef", 5u
            )) {
                dyn_undef(
                    preprocessor, source + argument_start,
                    line_end - argument_start
                );
            } else if (active && dyn_word(
                source + directive_start, directive_length, "pragma", 6u
            )) {
                if (dyn_word(
                    source + argument_start, line_end - argument_start,
                    "once", 4u
                )) preprocessor->once_files[file] = 1;
                else preprocessor->error = 1;
            } else if (active && dyn_word(
                source + directive_start, directive_length, "error", 5u
            )) preprocessor->error = 1;
            else if (active && dyn_word(
                source + directive_start, directive_length, "include", 7u
            )) {
                char opening;
                char closing;
                unsigned int name_start;
                unsigned int name_end;
                unsigned int included;
                if (argument_start >= line_end) {
                    preprocessor->error = 1;
                    return;
                }
                opening = source[argument_start];
                closing = opening == '"' ? '"' : '>';
                if (opening != '"' && opening != '<') {
                    preprocessor->error = 1;
                    return;
                }
                name_start = argument_start + 1u;
                name_end = name_start;
                while (name_end < line_end && source[name_end] != closing)
                    name_end += 1u;
                if (name_end >= line_end) {
                    preprocessor->error = 1;
                    return;
                }
                included = dyn_find_include(
                    preprocessor, file, source + name_start,
                    name_end - name_start, opening == '"'
                );
                if (included == 0xffffffffu) preprocessor->error = 1;
                else dyn_process_file(preprocessor, included, depth + 1u);
            } else if (active) preprocessor->error = 1;
            dyn_append(preprocessor, "\n", 1u);
        } else if (active) {
            dyn_expand_line(preprocessor, source, line_start, line_end);
            dyn_append(preprocessor, "\n", 1u);
        }
    }
    if (conditional_depth) preprocessor->error = 1;
    preprocessor->active_files[file] = 0;
}

int dyn_preprocess_project(
    const struct DynProjectFile *files,
    unsigned int file_count,
    const struct DynProjectText *include_roots,
    unsigned int include_root_count,
    const struct DynProjectText *definitions,
    unsigned int definition_count,
    char **output,
    unsigned int *output_length
) {
    struct DynPreprocessor preprocessor;
    unsigned int capacity = 4096u;
    unsigned int index = 0;
    while (index < file_count) {
        if (files[index].contents.length > 1048576u
            || capacity > 1048576u - files[index].contents.length)
            return 0;
        capacity += files[index].contents.length;
        index += 1u;
    }
    preprocessor.files = files;
    preprocessor.file_count = file_count;
    preprocessor.roots = include_roots;
    preprocessor.root_count = include_root_count;
    preprocessor.macro_capacity = file_count * 4u + definition_count + 16u;
    preprocessor.macros = malloc(
        preprocessor.macro_capacity * sizeof(struct DynMacro)
    );
    preprocessor.once_files = calloc(file_count, 1u);
    preprocessor.active_files = calloc(file_count, 1u);
    preprocessor.macro_count = 0u;
    preprocessor.output = malloc(capacity + 1u);
    preprocessor.length = 0u;
    preprocessor.capacity = capacity;
    preprocessor.error = !preprocessor.macros || !preprocessor.output
        || (file_count && (!preprocessor.once_files
            || !preprocessor.active_files));
    index = 0;
    while (index < definition_count && !preprocessor.error) {
        unsigned int length = 0;
        while (length < definitions[index].length
            && dyn_name_char(definitions[index].data[length])) length += 1u;
        {
            unsigned int replacement_start = length;
            if (replacement_start < definitions[index].length
                && definitions[index].data[replacement_start] == '=')
                replacement_start += 1u;
            dyn_define(
                &preprocessor, definitions[index].data, length,
                definitions[index].data + replacement_start,
                definitions[index].length - replacement_start
            );
        }
        index += 1u;
    }
    index = 0;
    while (index < file_count && !preprocessor.error) {
        if (files[index].kind == 1u)
            dyn_process_file(&preprocessor, index, 0u);
        index += 1u;
    }
    if (preprocessor.error) {
        if (preprocessor.output) free(preprocessor.output);
        if (preprocessor.once_files) free(preprocessor.once_files);
        if (preprocessor.active_files) free(preprocessor.active_files);
        free(preprocessor.macros);
        return 0;
    }
    preprocessor.output[preprocessor.length] = 0;
    free(preprocessor.once_files);
    free(preprocessor.active_files);
    free(preprocessor.macros);
    *output = preprocessor.output;
    *output_length = preprocessor.length;
    return 1;
}
