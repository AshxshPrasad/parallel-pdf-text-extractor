# Parallel PDF Text Extractor (OpenMP + C)

A real-world educational project demonstrating practical parallel computing using C, OpenMP, and the Poppler PDF library on Ubuntu Linux.

## Project Overview

This project extracts text from PDF files. Since extracting text from one page does not depend on any other page, this is an inherently parallelizable task. We use OpenMP to distribute the pages across multiple threads to achieve speedup.

## Why PDF Extraction is Parallelizable

In a PDF, pages are relatively independent. We can open the document, determine the total number of pages, and then instruct different CPU threads to extract text from different pages concurrently. 

However, since the lengths of pages vary (some might have a lot of text, some might have none), this creates an interesting load balancing problem which we can solve using OpenMP scheduling (`static`, `dynamic`, `guided`).

## Architecture

1. **Sequential Base**: A baseline implementation extracting pages from 0 to N.
2. **Parallel Implementation**: Uses `#pragma omp parallel for` to distribute the extraction workload.
3. **Result Storage**: A shared array `PageResult results[num_pages]` is allocated. Each thread writes its extracted text to its specific index (`results[page_num]`). This ensures the final output maintains the original PDF page order and avoids race conditions.

## Race Conditions and Thread Safety

**Race Conditions:** If multiple threads attempt to write to the `output.txt` file simultaneously using `fprintf`, the file stream's state will become corrupted or text will interleave incorrectly. We avoid this by having threads write to a memory array (our `results` struct array) based on their assigned page index, and a single sequential write phase happens at the end.

**Thread Safety:** The GLib-based Poppler API does not guarantee that concurrent calls to `poppler_document_get_page` on a shared `PopplerDocument` object are thread-safe without a global lock. To guarantee thread safety without introducing a serializing lock, this project uses a **thread-local document architecture**. Each OpenMP worker thread opens its own independent instance of the `PopplerDocument` from the file URI at the start of its parallel region. This introduces a slight memory and initialization overhead but ensures robust parallel execution.

## Dependencies

- **GCC** (with OpenMP support)
- **Poppler GLib** (`libpoppler-glib-dev`)
- **pkg-config**
- **Make**

## Installation

Run the following commands on Ubuntu:

```bash
sudo apt-get update
sudo apt-get install build-essential pkg-config libpoppler-glib-dev
```

## Compilation

We provide a `Makefile`.

```bash
make
make clean
```

The Makefile uses `-fopenmp` for OpenMP and `pkg-config --cflags --libs poppler-glib` to correctly link the Poppler library.

## Usage

```bash
# Sequential mode
./pdf_extractor input.pdf output.txt --mode sequential

# Parallel mode (defaults to max threads)
./pdf_extractor input.pdf output.txt --mode parallel

# Parallel mode with explicit threads and scheduling
./pdf_extractor input.pdf output.txt --mode parallel --threads 4 --schedule dynamic

# Benchmark mode (runs sequential, then parallel, checks correctness, and reports speedup)
./pdf_extractor input.pdf output_base --mode benchmark --threads 4
```

## Benchmarking & Amdahl's Law

We measure the *Sequential Time* ($T_s$) and the *Parallel Time* ($T_p$).

**Speedup** = $T_s / T_p$  
**Efficiency** = (Speedup / Number of Threads) * 100%

### Amdahl's Law
Speedup will not increase linearly with the number of threads forever. Amdahl's law states:
$$ Speedup \leq \frac{1}{(1-P) + \frac{P}{N}} $$
Where $P$ is the parallelizable fraction of the code, and $N$ is the number of threads. 
In our application, opening the PDF, allocating memory, and the final sequential writing of the array to the text file are *serial* portions of the code. Only the actual page extraction is parallelized. Thus, maximum speedup is bounded by those serial sections.

## Benchmark Methodology

To ensure scientifically reliable and repeatable measurements, the built-in benchmark performs the following rigorous steps:

1. **Hardware Detection**: The benchmark script reports the CPU model, physical/logical core counts, and OpenMP limits.
2. **Timing Region**: We strictly measure the **core page-extraction workload**. The timing starts *after* thread-local document initialization (using OpenMP barriers) and ends immediately after parallel extraction completes. File I/O is explicitly excluded from the timing.
3. **Repeated Runs**: For each thread configuration (1, 2, 4, 8, 16), the benchmark performs **2 warm-up runs** (ignored) and **5 measured runs**.
4. **Calculations**: Speedup and parallel efficiency are calculated by taking the *average* time of the 1-thread execution as the baseline, and comparing it against the *average* time of N-thread executions. Minimum, maximum, and sample standard deviation are also computed.
5. **Fairness**: Every thread count processes the exact same 481-page input PDF (`textbook.pdf`) using the same OpenMP schedule.
6. **Correctness Verification**: At the very end of the benchmarking suite, a sequential run and a max-thread parallel run are executed and their outputs are verified byte-for-byte to ensure the parallel optimizations did not corrupt extraction.
7. **CSV Output**: The script additionally produces a `benchmark_results.csv` file.

*Limitations*: The thread-local document architecture overhead, memory bandwidth saturation (especially on systems with high core counts but few memory channels), and operating-system thread scheduling variations will naturally bound the maximum achievable parallel efficiency.

## Scalability and Scheduling

You can test scalability by running `bash benchmark.sh <your_pdf.pdf>`.

If pages have vastly different amounts of text, you might experience *load imbalance*. 
- `schedule(static)` (default) divides the pages into chunks blindly.
- `schedule(dynamic)` assigns chunks to threads as they finish their previous work, improving balance if pages vary in complexity.

## Troubleshooting

- **No Output**: Ensure the PDF actually contains extractable text (not just scanned images).
- **Compilation errors**: Ensure `libpoppler-glib-dev` is installed.
- **Incorrect Order**: This shouldn't happen with our array-based architecture. If you modified it to `fprintf` inside the parallel loop, you introduced a race condition!
