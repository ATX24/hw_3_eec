# Energy-Efficient Computing
## Homework 3: Energy Cost of Programming Languages

In this assignment you will implement the same three algorithms in **Python**, **Java**, and
**C++**, then measure how much energy each implementation uses with Intel's RAPL (Running
Average Power Limit) energy counters. The algorithms are matrix multiplication, quicksort,
and mergesort. The algorithm is identical in all three languages; only the language changes.

You are given a small tool, `rapl`, that runs any command and reports the package and DRAM
energy used while it ran (Appendix A). You do not need to change your programs to use it.
Just put it in front of the command:

```
./rapl python3 bench.py matmul 400
./rapl java Bench matmul 400
./rapl ./bench matmul 400
```

Do this assignment on a UTCS lab machine. RAPL counters are not available in VMs, on macOS,
or on most AMD machines.

1. **Implementation.** Write three programs: `bench.py`, `Bench.java`, and `bench.cpp`.
   Each takes two command-line arguments, `<mode> N`, where mode is one of `matmul`,
   `quicksort`, `mergesort`, `gen-matrix`, or `gen-array`. Follow the input generator, output
   format, and pseudocode in Appendices B and C **exactly**.

   Rules:
   - Implement the algorithms yourself, exactly as written in the pseudocode. Do not use
     library sorts (`sorted`, `list.sort`, `Arrays.sort`, `std::sort`), NumPy, BLAS, threads,
     or any other "faster" variant. Do not reorder loops or add a cutoff to insertion sort.
   - Store matrices as flat, one-dimensional, row-major arrays. Use Python `list`s, Java
     primitive arrays (`double[]`, `int[]`), and C++ `std::vector`.
   - Compile C++ with `g++ -O2 -o bench bench.cpp`. Do not use `-march=native` or
     `-ffast-math`. Compile Java with `javac Bench.java` and run it with `java Bench ...`.
   - All three languages must print **identical** output for the same mode and N. Use these
     reference outputs to check your code:

     ```
     matmul n=200 checksum=1994893.094208
     quicksort n=300000 sorted=yes checksum=997411012
     mergesort n=300000 sorted=yes checksum=997411012
     ```

2. **Measurement.** Build the tool with `make -C rapl`.

   A. Measure the machine's idle power: `./rapl -r 5 sleep 10`. Report the mean package power
      \(P_{idle}\) and mean DRAM power, in watts.

   B. Run all nine (language, algorithm) combinations with **N = 400 for matmul** and
      **N = 2,000,000 for the sorts**. Use at least 5 repetitions each, and log the results
      to a CSV file, for example:

      ```
      ./rapl -r 5 -o results.csv -l py-matmul python3 bench.py matmul 400
      ```

   C. Run the `gen-matrix 400` and `gen-array 2000000` modes in each language the same way.
      These modes do everything except the algorithm: interpreter or JVM startup, input
      generation, and the checksum.

   D. Report a table with one row per (language, algorithm). Include mean runtime, package
      energy, DRAM energy, and *dynamic* package energy, defined as
      \[ E_{dyn} = E_{pkg} - P_{idle} \cdot t \]
      Also report *algorithm-only* dynamic energy: the dynamic energy of the full run minus
      the dynamic energy of the matching `gen-*` run.

3. **Analysis.** Answer briefly, using your numbers.

   A. For each algorithm, rank the three languages by total package energy and by dynamic
      energy, and give each as a ratio to C++. Does the ranking or the size of the gap change
      when you subtract idle power? Why does subtracting idle power matter so much on these
      machines? (Hint: what does RAPL actually measure, and how many cores does this socket
      have?)

   B. Compute the average power \(E_{pkg}/t\) for every run. Is energy roughly proportional
      to runtime here? Explain why average power barely changes between languages. Would you
      expect the same on a 4-core laptop running on battery?

   C. What fraction of the Java and Python energy is spent on startup and input generation
      rather than the algorithm itself? Run Java matmul at N = 100, 200, 300, and 400, and
      plot algorithm-only energy against N. Does the JIT compiler visibly affect the small
      sizes?

   D. Within each language, which uses less energy, quicksort or mergesort? Is the answer the
      same in all three languages? Use the DRAM energy numbers to support your explanation.

   E. Rebuild the C++ version with `-O0` and with `-O3`, then rerun matmul. Where does
      `-O0` C++ fall relative to Java and Python? What does that suggest about whether the
      energy differences come from the *language* or from the *implementation*
      (interpreter, JIT, or ahead-of-time compiler)?

   F. List three sources of error or noise in your RAPL measurements, and explain what you
      did (or could do) to reduce each one.

**Submit:** your three source files, `results.csv`, and a PDF write-up with your tables,
plot, and answers.

### Appendix A: the `rapl` tool

```
usage: rapl [-r REPEATS] [-o out.csv] [-l LABEL] COMMAND [ARGS...]
  -r N      run the command N times (default 1)
  -o FILE   append one CSV row per run to FILE
  -l LABEL  label for the CSV rows (default: the command line)
```

`rapl` reads the counters in `/sys/class/powercap/intel-rapl:*` immediately before starting
your command and again right after it exits. It handles counter wraparound. Your program's
output is passed through unchanged, and the energy report is printed to stderr:

```
[rapl] ./bench matmul 400
[rapl]   time                    0.085 s
[rapl]   package-0               4.118 J
[rapl]   package-0/dram          0.552 J
[rapl]   package-1               3.601 J
[rapl]   package-1/dram          0.497 J
[rapl]   package total           7.719 J   (avg 90.3 W)
[rapl]   dram total              1.049 J   (avg 12.3 W)
```

The lab machines have two sockets. Use the *package total* and *dram total* lines, which sum
both sockets. RAPL measures **everything** running on the socket, not just your process, so
check `uptime` first and run your experiments when the machine is quiet.

### Appendix B: input generator and output format

All three languages use the same pseudo-random generator, with 64-bit arithmetic and
`state` starting at 42:

```
next():
    state = (state * 1103515245 + 12345) & 0x7FFFFFFF
    return state
```

Call `next()` exactly once per element, in order.

**`matmul N` / `gen-matrix N`:** Fill `A` (N·N doubles) first, then `B`, with
`A[i] = (next() % 1000) / 1000.0`. Set `C` to N·N zeros. In `matmul` mode, compute
`C = A × B`; in `gen-matrix` mode, skip that step. Then add up the entries of `C` in index
order, starting from `0.0`, and print:

```
<mode> n=<N> checksum=<sum, 6 decimal places>
```

**`quicksort N` / `mergesort N` / `gen-array N`:** Fill an integer array `a` of length N with
`a[i] = next()`. Sort it with the chosen algorithm (`gen-array` does not sort). Then check
whether it is sorted, compute `checksum = sum over i of a[i] * (i + 1)` taken mod
1,000,000,007 (use 64-bit integers), and print:

```
<mode> n=<N> sorted=<yes|no> checksum=<checksum>
```

### Appendix C: pseudocode

**Matrix multiply** (naive i-j-k order, flat row-major arrays):

```
matmul(A, B, C, n):
    for i in 0 .. n-1:
        for j in 0 .. n-1:
            s = 0.0
            for k in 0 .. n-1:
                s += A[i*n + k] * B[k*n + j]
            C[i*n + j] = s
```

**Quicksort** (Lomuto partition, middle element as pivot, inclusive bounds; call
`quicksort(a, 0, n-1)`):

```
quicksort(a, lo, hi):
    if lo >= hi: return
    mid = lo + (hi - lo) / 2            // integer division
    swap a[mid], a[hi]
    pivot = a[hi]
    i = lo
    for j in lo .. hi-1:
        if a[j] < pivot:
            swap a[i], a[j]
            i = i + 1
    swap a[i], a[hi]
    quicksort(a, lo, i - 1)
    quicksort(a, i + 1, hi)
```

**Mergesort** (top-down, half-open bounds, one temporary array of length n allocated once
before the first call; call `mergesort(a, tmp, 0, n)`):

```
mergesort(a, tmp, lo, hi):
    if hi - lo < 2: return
    mid = (lo + hi) / 2                 // integer division
    mergesort(a, tmp, lo, mid)
    mergesort(a, tmp, mid, hi)
    i = lo; j = mid; k = lo
    while i < mid and j < hi:
        if a[i] <= a[j]: tmp[k] = a[i]; i++
        else:            tmp[k] = a[j]; j++
        k++
    while i < mid: tmp[k] = a[i]; i++; k++
    while j < hi:  tmp[k] = a[j]; j++; k++
    for k in lo .. hi-1: a[k] = tmp[k]
```

In Python, call `sys.setrecursionlimit(100000)` at the top of the file.
