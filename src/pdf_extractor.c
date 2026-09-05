#include <limits.h>
#include "pdf_extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poppler.h>
#include <omp.h>

int extract_pdf_sequential(const char *input_path, const char *output_path) {
    GError *error = NULL;

    char abs_path[PATH_MAX];
    if (realpath(input_path, abs_path) == NULL) {
        fprintf(stderr, "Error resolving absolute path for: %s\n", input_path);
        return -1;
    }

    // Convert absolute path to file URI, required by Poppler
    gchar *uri = g_filename_to_uri(abs_path, NULL, &error);
    if (!uri) {
        fprintf(stderr, "Error converting path to URI: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    // Open the PDF document
    PopplerDocument *document = poppler_document_new_from_file(uri, NULL, &error);
    g_free(uri);
    if (!document) {
        fprintf(stderr, "Error opening PDF: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    // Get number of pages
    int num_pages = poppler_document_get_n_pages(document);
    if (num_pages <= 0) {
        fprintf(stderr, "PDF has no pages or is invalid.\n");
        g_object_unref(document);
        return -1;
    }

    // Open output file
    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Error opening output file: %s\n", output_path);
        g_object_unref(document);
        return -1;
    }

    printf("Processing %d pages sequentially...\n", num_pages);

    // Start timing the core extraction work
    double start_time = omp_get_wtime();

    for (int i = 0; i < num_pages; i++) {
        PopplerPage *page = poppler_document_get_page(document, i);
        if (page) {
            char *text = poppler_page_get_text(page);
            
            fprintf(out, "================ PAGE %d ================\n\n", i + 1);
            if (text) {
                fprintf(out, "%s\n", text);
                g_free(text); // Free Poppler-allocated string
            } else {
                fprintf(out, "[No text found on this page]\n");
            }
            fprintf(out, "\n");
            
            g_object_unref(page); // Free the page object
        } else {
            fprintf(stderr, "Warning: Failed to read page %d\n", i + 1);
        }
    }

    // End timing
    double end_time = omp_get_wtime();
    double exec_time = end_time - start_time;

    printf("========================================\n");
    printf("Performance Results\n");
    printf("========================================\n");
    printf("Sequential Time : %.4f s\n", exec_time);
    printf("========================================\n");

    // Clean up
    fclose(out);
    g_object_unref(document);

    return 0;
}

int extract_pdf_parallel(const char *input_path, const char *output_path, int num_threads, const char *schedule_type) {
    GError *error = NULL;

    char abs_path[PATH_MAX];
    if (realpath(input_path, abs_path) == NULL) {
        fprintf(stderr, "Error resolving absolute path for: %s\n", input_path);
        return -1;
    }

    gchar *uri = g_filename_to_uri(abs_path, NULL, &error);
    if (!uri) {
        fprintf(stderr, "Error converting path to URI: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    PopplerDocument *doc_info = poppler_document_new_from_file(uri, NULL, &error);
    if (!doc_info) {
        fprintf(stderr, "Error opening PDF: %s\n", error->message);
        g_error_free(error);
        g_free(uri);
        return -1;
    }

    int num_pages = poppler_document_get_n_pages(doc_info);
    g_object_unref(doc_info);
    if (num_pages <= 0) {
        fprintf(stderr, "PDF has no pages or is invalid.\n");
        g_free(uri);
        return -1;
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Error opening output file: %s\n", output_path);
        g_free(uri);
        return -1;
    }

    // Allocate result array to avoid race conditions and preserve order
    PageResult *results = calloc(num_pages, sizeof(PageResult));
    if (!results) {
        fprintf(stderr, "Memory allocation failed for results array.\n");
        fclose(out);
        g_free(uri);
        return -1;
    }

    // Set number of threads
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }

    // Set scheduling
    omp_sched_t kind = omp_sched_static;
    if (strcmp(schedule_type, "dynamic") == 0) {
        kind = omp_sched_dynamic;
    } else if (strcmp(schedule_type, "guided") == 0) {
        kind = omp_sched_guided;
    }
    omp_set_schedule(kind, 0); // 0 means default chunk size

    printf("Processing %d pages in parallel (Threads: %d, Schedule: %s)...\n", 
           num_pages, num_threads > 0 ? num_threads : omp_get_max_threads(), schedule_type);

    double core_start = 0.0, core_end = 0.0;

    // The core parallel region
    #pragma omp parallel
    {
        GError *local_error = NULL;
        PopplerDocument *thread_doc = poppler_document_new_from_file(uri, NULL, &local_error);

        #pragma omp barrier

        #pragma omp single
        {
            core_start = omp_get_wtime();
        }

        if (thread_doc) {
            #pragma omp for schedule(runtime)
            for (int i = 0; i < num_pages; i++) {
                PopplerPage *page = poppler_document_get_page(thread_doc, i);
                if (page) {
                    char *text = poppler_page_get_text(page);
                    results[i].text = text; // Store result in thread-safe index
                    g_object_unref(page);
                } else {
                    results[i].text = NULL;
                }
            }
        }

        #pragma omp barrier

        #pragma omp single
        {
            core_end = omp_get_wtime();
        }

        if (thread_doc) {
            g_object_unref(thread_doc);
        }
    }

    double exec_time = core_end - core_start;

    // Sequential write phase (outside parallel region)
    for (int i = 0; i < num_pages; i++) {
        fprintf(out, "================ PAGE %d ================\n\n", i + 1);
        if (results[i].text) {
            fprintf(out, "%s\n", results[i].text);
            g_free(results[i].text); // Free Poppler-allocated string
        } else {
            fprintf(out, "[No text found on this page]\n");
        }
        fprintf(out, "\n");
    }

    free(results); // Free our array

    printf("========================================\n");
    printf("Performance Results\n");
    printf("========================================\n");
    printf("Parallel Time   : %.4f s\n", exec_time);
    printf("========================================\n");

    g_free(uri);
    fclose(out);

    return 0;
}

PageResult* extract_pdf_memory_sequential(const char *input_path, int *out_num_pages, ExtractionStats *stats) {
    GError *error = NULL;
    char abs_path[PATH_MAX];
    if (realpath(input_path, abs_path) == NULL) return NULL;

    gchar *uri = g_filename_to_uri(abs_path, NULL, &error);
    if (!uri) return NULL;

    double end_to_end_start = omp_get_wtime();

    PopplerDocument *document = poppler_document_new_from_file(uri, NULL, &error);
    g_free(uri);
    if (!document) return NULL;

    int num_pages = poppler_document_get_n_pages(document);
    if (num_pages <= 0) {
        g_object_unref(document);
        return NULL;
    }
    *out_num_pages = num_pages;

    PageResult *results = calloc(num_pages, sizeof(PageResult));
    if (!results) {
        g_object_unref(document);
        return NULL;
    }

    double core_start = omp_get_wtime();

    for (int i = 0; i < num_pages; i++) {
        PopplerPage *page = poppler_document_get_page(document, i);
        if (page) {
            results[i].text = poppler_page_get_text(page);
            g_object_unref(page);
        }
    }

    stats->core_time = omp_get_wtime() - core_start;
    g_object_unref(document);
    stats->end_to_end_time = omp_get_wtime() - end_to_end_start;

    return results;
}

PageResult* extract_pdf_memory_parallel(const char *input_path, int num_threads, const char *schedule_type, int chunk_size, int *out_num_pages, ExtractionStats *stats) {
    GError *error = NULL;
    char abs_path[PATH_MAX];
    if (realpath(input_path, abs_path) == NULL) return NULL;

    gchar *uri = g_filename_to_uri(abs_path, NULL, &error);
    if (!uri) return NULL;

    double end_to_end_start = omp_get_wtime();

    PopplerDocument *doc_info = poppler_document_new_from_file(uri, NULL, &error);
    if (!doc_info) {
        g_free(uri);
        return NULL;
    }
    int num_pages = poppler_document_get_n_pages(doc_info);
    g_object_unref(doc_info);

    if (num_pages <= 0) {
        g_free(uri);
        return NULL;
    }
    *out_num_pages = num_pages;

    PageResult *results = calloc(num_pages, sizeof(PageResult));
    if (!results) {
        g_free(uri);
        return NULL;
    }

    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }

    omp_sched_t kind = omp_sched_static;
    if (strcmp(schedule_type, "dynamic") == 0) kind = omp_sched_dynamic;
    else if (strcmp(schedule_type, "guided") == 0) kind = omp_sched_guided;
    omp_set_schedule(kind, chunk_size);

    double core_start = 0.0, core_end = 0.0;

    #pragma omp parallel
    {
        GError *local_error = NULL;
        PopplerDocument *thread_doc = poppler_document_new_from_file(uri, NULL, &local_error);

        #pragma omp barrier

        #pragma omp single
        {
            core_start = omp_get_wtime();
        }

        if (thread_doc) {
            #pragma omp for schedule(runtime)
            for (int i = 0; i < num_pages; i++) {
                PopplerPage *page = poppler_document_get_page(thread_doc, i);
                if (page) {
                    results[i].text = poppler_page_get_text(page);
                    g_object_unref(page);
                } else {
                    results[i].text = NULL;
                }
            }
        }

        #pragma omp barrier

        #pragma omp single
        {
            core_end = omp_get_wtime();
        }

        if (thread_doc) {
            g_object_unref(thread_doc);
        }
    }

    stats->core_time = core_end - core_start;
    g_free(uri);
    stats->end_to_end_time = omp_get_wtime() - end_to_end_start;

    return results;
}
