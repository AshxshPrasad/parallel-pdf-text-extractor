#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <omp.h>
#include <glib.h>
#include "benchmark.h"
#include "pdf_extractor.h"

static int compare_files(const char *file1, const char *file2) {
    FILE *f1 = fopen(file1, "r");
    FILE *f2 = fopen(file2, "r");
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return 0;
    }

    int c1, c2;
    do {
        c1 = fgetc(f1);
        c2 = fgetc(f2);
        if (c1 != c2) {
            fclose(f1);
            fclose(f2);
            return 0;
        }
    } while (c1 != EOF && c2 != EOF);

    fclose(f1);
    fclose(f2);
    return c1 == c2;
}

static int get_max_cores() {
    FILE *fp = popen("nproc", "r");
    if (fp) {
        char buf[32];
        if (fgets(buf, sizeof(buf), fp)) {
            pclose(fp);
            return atoi(buf);
        }
        pclose(fp);
    }
    return omp_get_max_threads();
}

int run_benchmark(const char *input_path, const char *seq_out, const char *par_out, int requested_threads, const char *requested_schedule) {
    (void)requested_threads;
    (void)requested_schedule;
    printf("Starting Benchmark...\n\n");

    int cores = get_max_cores();
    int thread_counts[] = {1, 2, 4, 8, 16};
    int num_configs = 5;
    
    const char *schedules[] = {"static", "dynamic", "guided"};
    int num_schedules = 3;

    int chunk_sizes[] = {1, 2, 4, 8, 16};
    int num_chunks = 5;

    printf("==============================================================\n");
    printf("System CPU Information\n");
    printf("==============================================================\n");
    if (system("lscpu | grep -E 'Model name|CPU\\(s\\):|Core|Thread'") == -1) {
        // Ignored
    }
    printf("OpenMP max threads: %d\n", omp_get_max_threads());
    printf("Detected logical CPUs: %d\n", cores);
    printf("==============================================================\n\n");

    FILE *csv = fopen("benchmark_results.csv", "w");
    if (csv) {
        fprintf(csv, "schedule,chunk_size,threads,average_core_time,min_core_time,max_core_time,stddev,speedup,efficiency,avg_end_to_end\n");
    }

    int warmups = 2;
    int runs = 5;
    
    printf("==============================================================\n");
    printf("Parallel PDF Extraction Benchmark\n");
    printf("==============================================================\n");
    printf("PDF: %s\n", input_path);
    printf("Warm-up runs: %d\n", warmups);
    printf("Measured runs: %d\n\n", runs);
    
    printf("Sched   | Chunk | Threads | Avg Core | Min Core | Max Core | Std Dev | E2E Avg  | Speedup | Efficiency\n");
    printf("----------------------------------------------------------------------------------------------------------\n");

    // Phase 1: Schedules Test (default chunk size = 0)
    for (int s = 0; s < num_schedules; s++) {
        const char *current_sched = schedules[s];
        double avg_seq_core = 0.0;
        
        for (int config = 0; config < num_configs; config++) {
            int t = thread_counts[config];
            if (t > cores && t != 1) {
                continue;
            }

            double times[5]; // Store core times
            double e2e_sum = 0.0;
            
            for (int r = 0; r < warmups + runs; r++) {
                ExtractionStats stats;
                int num_pages = 0;
                PageResult *res;
                
                // Apples-to-apples parallel scaling baseline (chunk size = 0 for default)
                res = extract_pdf_memory_parallel(input_path, t, current_sched, 0, &num_pages, &stats);
                
                if (res) {
                    for (int i = 0; i < num_pages; i++) {
                        if (res[i].text) g_free(res[i].text);
                    }
                    free(res);
                } else {
                    fprintf(stderr, "Extraction failed during benchmark run\n");
                    if (csv) fclose(csv);
                    return 1;
                }
                
                if (r >= warmups) {
                    times[r - warmups] = stats.core_time;
                    e2e_sum += stats.end_to_end_time;
                }
            }
            
            double sum = 0.0, min_time = times[0], max_time = times[0];
            for (int r = 0; r < runs; r++) {
                sum += times[r];
                if (times[r] < min_time) min_time = times[r];
                if (times[r] > max_time) max_time = times[r];
            }
            
            double avg = sum / runs;
            double avg_e2e = e2e_sum / runs;
            
            double variance_sum = 0.0;
            for (int r = 0; r < runs; r++) {
                variance_sum += (times[r] - avg) * (times[r] - avg);
            }
            double stddev = sqrt(variance_sum / (runs - 1));
            
            if (t == 1) {
                avg_seq_core = avg;
            }
            
            double speedup = avg_seq_core / avg;
            double efficiency = (speedup / t) * 100.0;
            
            printf("%-7s | %-5s | %-7d | %-8.4f | %-8.4f | %-8.4f | %-7.4f | %-8.4f | %-6.3fx | %-5.2f%%\n",
                   current_sched, "def", t, avg, min_time, max_time, stddev, avg_e2e, speedup, efficiency);
                   
            if (csv) {
                fprintf(csv, "%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%.3f,%.2f,%.4f\n",
                        current_sched, 0, t, avg, min_time, max_time, stddev, speedup, efficiency, avg_e2e);
            }
        }
        printf("----------------------------------------------------------------------------------------------------------\n");
    }

    // Phase 2: Chunk Size Test (Guided schedule, 8 threads)
    const char *chunk_sched = "guided";
    int chunk_threads = 8;
    double avg_seq_core_chunk = 0.0;
    
    for (int c = 0; c < num_chunks; c++) {
        int chunk = chunk_sizes[c];
        
        // For speedup calc, run the 1-thread baseline with the same chunk size
        double sum_1 = 0.0;
        for (int r = 0; r < warmups + runs; r++) {
            ExtractionStats stats;
            int num_pages = 0;
            PageResult *res = extract_pdf_memory_parallel(input_path, 1, chunk_sched, chunk, &num_pages, &stats);
            if (res) {
                for (int i = 0; i < num_pages; i++) {
                    if (res[i].text) g_free(res[i].text);
                }
                free(res);
            }
            if (r >= warmups) sum_1 += stats.core_time;
        }
        avg_seq_core_chunk = sum_1 / runs;

        double times[5];
        double e2e_sum = 0.0;
        
        for (int r = 0; r < warmups + runs; r++) {
            ExtractionStats stats;
            int num_pages = 0;
            PageResult *res = extract_pdf_memory_parallel(input_path, chunk_threads, chunk_sched, chunk, &num_pages, &stats);
            
            if (res) {
                for (int i = 0; i < num_pages; i++) {
                    if (res[i].text) g_free(res[i].text);
                }
                free(res);
            } else {
                fprintf(stderr, "Extraction failed during chunk test\n");
                if (csv) fclose(csv);
                return 1;
            }
            
            if (r >= warmups) {
                times[r - warmups] = stats.core_time;
                e2e_sum += stats.end_to_end_time;
            }
        }
        
        double sum = 0.0, min_time = times[0], max_time = times[0];
        for (int r = 0; r < runs; r++) {
            sum += times[r];
            if (times[r] < min_time) min_time = times[r];
            if (times[r] > max_time) max_time = times[r];
        }
        
        double avg = sum / runs;
        double avg_e2e = e2e_sum / runs;
        
        double variance_sum = 0.0;
        for (int r = 0; r < runs; r++) {
            variance_sum += (times[r] - avg) * (times[r] - avg);
        }
        double stddev = sqrt(variance_sum / (runs - 1));
        
        double speedup = avg_seq_core_chunk / avg;
        double efficiency = (speedup / chunk_threads) * 100.0;
        
        printf("%-7s | %-5d | %-7d | %-8.4f | %-8.4f | %-8.4f | %-7.4f | %-8.4f | %-6.3fx | %-5.2f%%\n",
               chunk_sched, chunk, chunk_threads, avg, min_time, max_time, stddev, avg_e2e, speedup, efficiency);
               
        if (csv) {
            fprintf(csv, "%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%.3f,%.2f,%.4f\n",
                    chunk_sched, chunk, chunk_threads, avg, min_time, max_time, stddev, speedup, efficiency, avg_e2e);
        }
    }
    printf("----------------------------------------------------------------------------------------------------------\n");
    
    printf("==============================================================\n\n");
    if (csv) fclose(csv);

    printf("Verifying Correctness...\n");
    
    int max_t = cores > 16 ? 16 : cores;
    if (max_t < 1) max_t = 1;
    
    if (extract_pdf_sequential(input_path, seq_out) != 0) {
        fprintf(stderr, "Correctness seq write failed\n");
        return 1;
    }
    if (extract_pdf_parallel(input_path, par_out, max_t, "static") != 0) {
        fprintf(stderr, "Correctness par write failed\n");
        return 1;
    }
    
    int is_correct = compare_files(seq_out, par_out);
    if (is_correct) {
        printf("Correctness Check: PASS (Sequential output == Parallel output)\n\n");
    } else {
        printf("Correctness Check: FAIL (Outputs do not match!)\n\n");
        return 1;
    }
    
    return 0;
}
