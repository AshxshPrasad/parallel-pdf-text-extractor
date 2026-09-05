# Parallel PDF Text Extractor

A C-based PDF text extraction tool that uses **OpenMP** for page-level parallelism and **Poppler GLib** for PDF processing.

The project explores practical parallel computing concepts including shared-memory parallelism, workload distribution, OpenMP scheduling, thread scalability, speedup, parallel efficiency, and load balancing.

---

## Features

- PDF text extraction using Poppler GLib
- Sequential and OpenMP-parallel extraction modes
- Page-level parallelism
- Thread-local Poppler documents for safer concurrent PDF processing
- Preserves original page ordering
- Static, dynamic, and guided OpenMP scheduling
- Configurable OpenMP thread count
- Automated performance benchmarking
- Warm-up runs and repeated measurements
- Average, minimum, maximum, and standard deviation reporting
- Speedup and parallel-efficiency calculations
- Core extraction and end-to-end timing
- Guided scheduling chunk-size experiments
- Byte-for-byte correctness verification
- CSV benchmark output
- Hardware/CPU information reporting

---

## How It Works

PDF pages can generally be processed independently, making page-level text extraction a suitable workload for parallelization.

The application divides the pages among OpenMP threads:

```text
                    PDF
                     |
             Determine page count
                     |
          +----------+----------+
          |                     |
     Sequential             Parallel
          |                     |
   One PDF document      One document/thread
          |                     |
          |              +------+------+
          |              |             |
          |           Thread 1      Thread 2 ...
          |              |             |
          |           Page i        Page j
          |              |             |
          |              +------+------+
          |                     |
          +----------+----------+
                     |
              Ordered results[]
                     |
              Sequential output
                     |
                  Text file



Each page's extracted text is stored at its corresponding page index:

results[page_number]

This allows threads to perform extraction concurrently while the final output is reconstructed sequentially in the original PDF order.

Thread Safety

The parallel implementation uses a thread-local PopplerDocument architecture.

Instead of allowing multiple OpenMP threads to operate on the same document object, each worker thread opens its own independent Poppler document instance.

OpenMP Parallel Region

Thread 0 ──> PopplerDocument 0 ──> pages
Thread 1 ──> PopplerDocument 1 ──> pages
Thread 2 ──> PopplerDocument 2 ──> pages
...
Thread N ──> PopplerDocument N ──> pages

This avoids relying on a global lock around page extraction and allows the extraction workload to remain parallel.

The trade-off is additional document initialization and memory overhead for each worker thread.

Avoiding Race Conditions

Threads do not write directly to the final output file during parallel extraction.

Instead:

Each thread extracts a page.
The extracted text is stored in results[page_index].
The parallel region completes.
Results are written sequentially in page order.

For example:

Thread 2 → Page 8  → results[8]
Thread 0 → Page 1  → results[1]
Thread 3 → Page 12 → results[12]
Thread 1 → Page 4  → results[4]

The final output is still:

Page 1
Page 2
Page 3
...
Page 12

regardless of which thread finished first.

OpenMP Scheduling

The extractor supports three OpenMP scheduling strategies.

Static
schedule(static)

Iterations are distributed ahead of time with low scheduling overhead.

This works well when page-processing costs are relatively uniform, but can suffer from load imbalance when some pages require substantially more processing.

Dynamic
schedule(dynamic)

Threads request additional work as they finish their current assignments.

This can improve load balancing for workloads with varying page-processing costs, at the cost of additional scheduling overhead.

Guided
schedule(guided)

Guided scheduling begins with larger chunks and progressively decreases the chunk size.

It provides a compromise between the low overhead of static scheduling and the adaptive workload distribution of dynamic scheduling.

Requirements

The project is designed for Ubuntu/Linux and requires:

GCC
OpenMP
Poppler GLib
GLib
pkg-config
Make

Install the required packages on Ubuntu:

sudo apt update
sudo apt install build-essential pkg-config libpoppler-glib-dev
Building

Clone the repository and enter the project directory:

git clone <repository-url>
cd parallel-pdf-text-extractor

Build:

make

The project is compiled with:

-Wall -Wextra -Wpedantic -O2 -fopenmp

Clean the build:

make clean
Usage
Sequential extraction
./pdf_extractor input.pdf output.txt --mode sequential
Parallel extraction
./pdf_extractor input.pdf output.txt --mode parallel
Specify the number of threads
./pdf_extractor input.pdf output.txt \
    --mode parallel \
    --threads 8
Select a scheduling strategy
./pdf_extractor input.pdf output.txt \
    --mode parallel \
    --threads 8 \
    --schedule guided

Supported schedules:

static
dynamic
guided
Help
./pdf_extractor --help
Benchmarking

The project includes an automated benchmark:

./benchmark.sh path/to/document.pdf

For example:

./benchmark.sh pdf/my_document.pdf

The benchmark automatically evaluates:

Threads:
1
2
4
8
16

and the following scheduling strategies:

static
dynamic
guided

It also evaluates guided scheduling with different chunk sizes:

guided,1
guided,2
guided,4
guided,8
guided,16
Benchmark Methodology

Each configuration is tested using:

2 warm-up runs
5 measured runs

The benchmark reports:

Average core extraction time
Minimum core extraction time
Maximum core extraction time
Sample standard deviation
Average end-to-end time
Speedup
Parallel efficiency
Core Extraction Timing

The primary performance measurement covers the parallel page-extraction workload.

Thread-local Poppler document initialization occurs before the core timing region.

File output is also excluded from the core extraction timing.

This makes the measurement focused on the part of the application being parallelized.

Speedup

Speedup is calculated as:

Speedup(N) = T1 / TN

where:

T1 = average extraction time using one parallel worker
TN = average extraction time using N workers
Parallel Efficiency
Efficiency = Speedup / N × 100

where N is the number of OpenMP threads.

Benchmark Results

A 481-page PDF was used as the primary benchmark workload on:

CPU: AMD Ryzen 7 7840HS
Physical cores: 8
Logical CPUs: 16
Threads per core: 2
Guided Thread Scaling
Threads	Avg Core Time	Speedup	Efficiency
1	1.1531 s	1.000×	100.00%
2	0.6116 s	1.885×	94.27%
4	0.3172 s	3.635×	90.89%
8	0.1868 s	6.173×	77.16%
16	0.1706 s	6.758×	42.24%

The best measured core extraction time was:

0.1706 seconds

using 16 OpenMP threads with guided scheduling.

However, increasing from 8 to 16 threads produced only a modest additional improvement. This demonstrates diminishing returns when moving beyond the processor's 8 physical cores onto its 16 logical threads.

Scheduling Comparison at 8 Threads
Schedule	Avg Core Time	Speedup	Efficiency
Static	0.2411 s	4.756×	59.44%
Dynamic	0.2225 s	5.194×	64.92%
Guided	0.1868 s	6.173×	77.16%

For this workload, guided scheduling provided the best measured performance at 8 threads.

The result suggests that page-processing costs are not completely uniform, making adaptive workload distribution beneficial.

Guided Chunk Size at 8 Threads
Chunk Size	Avg Core Time	Speedup	Efficiency
1	0.1839 s	6.220×	77.74%
2	0.1858 s	6.120×	76.50%
4	0.1823 s	6.185×	77.31%
8	0.1868 s	5.966×	74.58%
16	0.1924 s	5.808×	72.60%

Small guided chunk sizes performed best in this experiment, while larger chunks showed a gradual performance decrease.

The differences between chunk sizes 1–4 are small, so these results should not be interpreted as proving a universally optimal chunk size.

Correctness Verification

Performance improvements are only useful if the extracted output remains correct.

After benchmarking, the application performs a full sequential extraction and a full parallel extraction.

The resulting files are compared byte-for-byte.

Example:

Correctness Check: PASS
(Sequential output == Parallel output)

This verifies that parallel execution preserves the extracted text and page ordering.

Amdahl's Law

The complete application contains both parallel and non-parallel portions.

Conceptually:

PDF initialization
       |
Memory allocation
       |
Parallel page extraction  <── OpenMP
       |
Sequential output

Amdahl's Law describes the theoretical limitation on speedup:

             1
S(N) = ---------------
        (1-P) + P/N

where:

P is the parallelizable fraction
N is the number of threads

The benchmark's core timing intentionally focuses on the page-extraction workload, while the complete application still contains initialization, synchronization, memory-management, and output overhead.

Therefore, increasing the number of threads does not produce unlimited linear speedup.

Scalability Observations

The benchmark demonstrates several practical parallel-computing behaviors:

Increasing thread count improves performance

Performance improves significantly from 1 to 8 threads.

Speedup is not linear

Doubling the number of threads does not consistently double performance.

Efficiency decreases at higher thread counts

This is particularly visible when moving from 8 to 16 logical threads.

Scheduling strategy matters

For the tested PDF, guided scheduling performed better than static and dynamic scheduling at 8 threads.

Workload characteristics matter

PDF pages can have different amounts and types of content, producing different extraction costs. Scheduling strategies therefore affect how effectively CPU work is balanced.

Limitations

The benchmark results depend on:

PDF structure and page complexity
CPU frequency and thermal state
Operating-system scheduling
Poppler implementation and behavior
Thread-local document initialization overhead
Memory/cache behavior
OpenMP runtime implementation

Therefore, the measured results should be interpreted as results for the tested workload and hardware rather than universal performance characteristics of PDF extraction.

Project Structure
parallel-pdf-text-extractor/
├── include/
│   ├── benchmark.h
│   └── pdf_extractor.h
├── src/
│   ├── benchmark.c
│   ├── main.c
│   └── pdf_extractor.c
├── tests/
├── benchmark.sh
├── Makefile
├── README.md
└── .gitignore

PDF files, generated output, compiled binaries, object files, and benchmark-generated CSV files are excluded from version control.

License

This project is intended for educational and portfolio use.

If you reuse or extend the project, please review the licensing requirements of the Poppler and GLib libraries used by the application.
