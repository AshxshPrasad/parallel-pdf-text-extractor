#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "benchmark.h"
#include "pdf_extractor.h"

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <input.pdf> <output.txt> [OPTIONS]\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --mode <sequential|parallel>  Extraction mode (default: sequential)\n");
    fprintf(stderr, "  --threads <N>                 Number of OpenMP threads (default: system max)\n");
    fprintf(stderr, "  --schedule <static|dynamic|guided> OpenMP schedule (default: static)\n");
    fprintf(stderr, "  --help                        Show this help message\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];
    
    int is_sequential = 1; // Default
    int num_threads = 0;   // 0 means let OpenMP decide
    const char *schedule = "static";

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "sequential") == 0) {
                is_sequential = 1;
            } else if (strcmp(argv[i+1], "parallel") == 0) {
                is_sequential = 0;
            } else if (strcmp(argv[i+1], "benchmark") == 0) {
                is_sequential = 2; // 2 means benchmark mode
            } else {
                fprintf(stderr, "Error: Unknown mode '%s'\n", argv[i+1]);
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[i+1]);
            if (num_threads <= 0) {
                fprintf(stderr, "Error: Threads must be > 0\n");
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "--schedule") == 0 && i + 1 < argc) {
            schedule = argv[i+1];
            if (strcmp(schedule, "static") != 0 && 
                strcmp(schedule, "dynamic") != 0 && 
                strcmp(schedule, "guided") != 0) {
                fprintf(stderr, "Error: Invalid schedule '%s'\n", schedule);
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Warning: Ignoring unknown argument '%s'\n", argv[i]);
        }
    }

    if (is_sequential == 1) {
        printf("Mode: Sequential\n");
        if (extract_pdf_sequential(input_path, output_path) != 0) {
            fprintf(stderr, "Failed to extract text sequentially.\n");
            return 1;
        }
    } else if (is_sequential == 0) {
        printf("Mode: Parallel\n");
        if (extract_pdf_parallel(input_path, output_path, num_threads, schedule) != 0) {
            fprintf(stderr, "Failed to extract text in parallel.\n");
            return 1;
        }
    } else if (is_sequential == 2) {
        char seq_out[256];
        char par_out[256];
        snprintf(seq_out, sizeof(seq_out), "%s.seq", output_path);
        snprintf(par_out, sizeof(par_out), "%s.par", output_path);
        if (run_benchmark(input_path, seq_out, par_out, num_threads, schedule) != 0) {
            return 1;
        }
        printf("Benchmark completed. Sequential output in %s, Parallel output in %s.\n", seq_out, par_out);
        return 0; // Don't print the single output message
    }
    
    printf("Successfully wrote extracted text to %s\n", output_path);
    return 0;
}
