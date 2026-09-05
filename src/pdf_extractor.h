#ifndef PDF_EXTRACTOR_H
#define PDF_EXTRACTOR_H

#include <stddef.h>

// Struct to hold extracted text per page
typedef struct {
    char *text;
} PageResult;

typedef struct {
    double core_time;
    double end_to_end_time;
} ExtractionStats;

// Original functions (preserves CLI behavior)
int extract_pdf_sequential(const char *input_path, const char *output_path);
int extract_pdf_parallel(const char *input_path, const char *output_path, int num_threads, const char *schedule_type);

// Benchmark functions (returns results in memory without writing to a file)
PageResult* extract_pdf_memory_sequential(const char *input_path, int *out_num_pages, ExtractionStats *stats);
PageResult* extract_pdf_memory_parallel(const char *input_path, int num_threads, const char *schedule_type, int chunk_size, int *out_num_pages, ExtractionStats *stats);

#endif // PDF_EXTRACTOR_H
