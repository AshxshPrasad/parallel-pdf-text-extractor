#ifndef BENCHMARK_H
#define BENCHMARK_H

// Run both sequential and parallel extraction and compare the results
int run_benchmark(const char *input_path, const char *seq_out, const char *par_out, int num_threads, const char *schedule);

#endif // BENCHMARK_H
