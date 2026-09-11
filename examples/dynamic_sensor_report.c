/*
 * Dynamic sensor report demo.
 *
 * Input protocol:
 *   count, followed by count signed 32-bit sensor readings.
 *
 * The program emits:
 *   original count, unique count, whether input was already sorted,
 *   minimum, maximum, median, average, then ten last-digit histogram buckets.
 *
 * It demonstrates a heap-backed vector and report, realloc growth, calloc,
 * every mem* routine, and a runtime-sized stack array used by merge sort.
 */

enum { MAX_SAMPLES = 256, HISTOGRAM_BUCKETS = 10 };

struct SampleSeries {
    int *values;
    unsigned int count;
    unsigned int capacity;
};

struct SensorReport {
    unsigned int original_count;
    unsigned int unique_count;
    unsigned int already_sorted;
    int minimum;
    int maximum;
    int median;
    int average;
    unsigned int *last_digit_histogram;
};

void series_init(struct SampleSeries *series) {
    memset(series, 0, sizeof(struct SampleSeries));
}

int series_push(struct SampleSeries *series, int value) {
    if (series->count == series->capacity) {
        unsigned int new_capacity = series->capacity ? series->capacity * 2u : 8u;
        int *larger;
        if (new_capacity > MAX_SAMPLES) new_capacity = MAX_SAMPLES;
        if (new_capacity == series->capacity) return 0;
        larger = realloc(series->values, new_capacity * sizeof(int));
        if (!larger) return 0;
        series->values = larger;
        series->capacity = new_capacity;
    }
    series->values[series->count] = value;
    series->count += 1;
    return 1;
}

void merge_range(
    int *values,
    int *scratch,
    unsigned int left,
    unsigned int middle,
    unsigned int right
) {
    unsigned int a = left, b = middle, out = left, index;
    while (a < middle && b < right) {
        if (values[a] <= values[b]) scratch[out++] = values[a++];
        else scratch[out++] = values[b++];
    }
    while (a < middle) scratch[out++] = values[a++];
    while (b < right) scratch[out++] = values[b++];
    for (index = left; index < right; index++) values[index] = scratch[index];
}

void merge_sort_range(
    int *values,
    int *scratch,
    unsigned int left,
    unsigned int right
) {
    unsigned int middle;
    if (right - left < 2) return;
    middle = left + (right - left) / 2u;
    merge_sort_range(values, scratch, left, middle);
    merge_sort_range(values, scratch, middle, right);
    merge_range(values, scratch, left, middle, right);
}

void sort_samples(int *values, unsigned int count) {
    /* The VLA exists only while sorting; persistent data stays on the heap. */
    int scratch[count];
    memset(scratch, 0, count * sizeof(int));
    merge_sort_range(values, scratch, 0, count);
}

unsigned int remove_duplicates(int *values, unsigned int count) {
    unsigned int index = 1;
    while (index < count) {
        if (values[index] == values[index - 1]) {
            /* memmove is required because these ranges overlap. */
            memmove(
                values + index,
                values + index + 1,
                (count - index - 1) * sizeof(int)
            );
            count -= 1;
        } else {
            index += 1;
        }
    }
    return count;
}

struct SensorReport *analyze(struct SampleSeries *series) {
    struct SensorReport *report;
    int *original;
    unsigned int bytes = series->count * sizeof(int);
    unsigned int index;
    int total = 0;

    if (!series->count) return 0;

    original = malloc(bytes);
    report = malloc(sizeof(struct SensorReport));
    if (!original || !report) {
        free(original);
        free(report);
        return 0;
    }
    memcpy(original, series->values, bytes);

    sort_samples(series->values, series->count);
    memset(report, 0, sizeof(struct SensorReport));
    report->original_count = series->count;
    report->already_sorted = memcmp(original, series->values, bytes) == 0;
    free(original);

    series->count = remove_duplicates(series->values, series->count);
    report->unique_count = series->count;
    report->minimum = series->values[0];
    report->maximum = series->values[series->count - 1];
    report->median = series->values[series->count / 2u];
    report->last_digit_histogram =
        calloc(HISTOGRAM_BUCKETS, sizeof(unsigned int));
    if (!report->last_digit_histogram) {
        free(report);
        return 0;
    }

    for (index = 0; index < series->count; index++) {
        int value = series->values[index];
        unsigned int magnitude =
            value < 0 ? 0u - (unsigned int)value : (unsigned int)value;
        total += value;
        report->last_digit_histogram[magnitude % HISTOGRAM_BUCKETS] += 1;
    }
    report->average = total / (int)series->count;
    return report;
}

void emit_report(const struct SensorReport *report) {
    unsigned int index;
    output(report->original_count);
    output(report->unique_count);
    output(report->already_sorted);
    output((unsigned int)report->minimum);
    output((unsigned int)report->maximum);
    output((unsigned int)report->median);
    output((unsigned int)report->average);
    for (index = 0; index < HISTOGRAM_BUCKETS; index++) {
        output(report->last_digit_histogram[index]);
    }
}

void destroy_report(struct SensorReport *report) {
    if (!report) return;
    free(report->last_digit_histogram);
    free(report);
}

int main(void) {
    struct SampleSeries series;
    struct SensorReport *report;
    unsigned int requested = input();
    unsigned int index;
    unsigned int result;

    if (!requested || requested > MAX_SAMPLES) return 0;
    series_init(&series);
    for (index = 0; index < requested; index++) {
        if (!series_push(&series, (int)input())) {
            free(series.values);
            return 0;
        }
    }

    report = analyze(&series);
    if (!report) {
        free(series.values);
        return 0;
    }
    emit_report(report);
    result = report->unique_count;
    destroy_report(report);
    free(series.values);
    return (int)result;
}
