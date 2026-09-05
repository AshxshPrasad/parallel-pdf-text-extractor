#!/bin/bash
cat << 'INNER_EOF' >> src/pdf_extractor.c

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

PageResult* extract_pdf_memory_parallel(const char *input_path, int num_threads, const char *schedule_type, int *out_num_pages, ExtractionStats *stats) {
    GError *error = NULL;
    char abs_path[PATH_MAX];
    if (realpath(input_path, abs_path) == NULL) return NULL;

    gchar *uri = g_filename_to_uri(abs_path, NULL, &error);
    if (!uri) return NULL;

    double end_to_end_start = omp_get_wtime();

    // Open once sequentially just to get the page count
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
    omp_set_schedule(kind, 0);

    double core_start = 0.0, core_end = 0.0;

    #pragma omp parallel
    {
        // Each thread opens its own PopplerDocument for thread safety
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
INNER_EOF
chmod +x patch_pdf_extractor.sh
./patch_pdf_extractor.sh
