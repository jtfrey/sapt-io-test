# sapt-io-test

I wrote a C program that implements the index-shuffling file i/o in Byungkyun's SAPT-style test.  The i/o calls are virtualized via a callback interface so that different i/o drivers can be tried:  the code currently includes drivers that use a Unix file descriptor versus the C stream-based i/o library.

Various algorithms are implemented, ranging from the straightforward scalar iteration over all three indices in different sequences to iteration over j alone with in-memory matrix transpose.

```
[frey@login01.darwin sapt-io-test]$ ./jki_to_jik --help
usage:

    ./jki_to_jik {options}

  options:

    -h/--help                    show this information
    -1 #, --n1=#                 range of index i
    -2 #, --n2=#                 range of index j
    -3 #, --n3=#                 range of index k
    -i <filepath>,               read (or possibly init) this file
        --input=<filepath>         as the source
    -o <filepath>,               write this file as the destination
        --output=<filepath>
    -x, --exact-dims             file sizes must exactly match the
                                   n1/n2/n3 dimensions
    -a <algorithm>,              use this specific i/o algorithm
        --algorithm=<algorithm>    in the input init and file processing
    -d <driver>,                 use this specific i/o driver for all
        --driver=<driver>          file access
    -I, --init-input             generate newly-initialized data in
                                   in the input file

  <algorithm>:
    jki_map         iterates in sequence j, k, i, reading from input
                    then writing to output (this is the default)
    jik_map         iterates in sequence j, i, k, reading from input
                    then writing to output
    ijk_map         iterates in sequence i, j, k, reading from input
                    then writing to output
    vector_input    1xn1 chunks are read from input then mapped by
                    index iteration to the output (requires n3 words of
                    memory)
    vector_output   1xn3 chunks are mapped by index iteration from the
                    input then written to the output (requires n3 words
                    of memory)
    matrix          n1xn3 chunks are read from input then transposed
                    in memory and written en masse to the output
                    (requires 2 x n1 x n3 words of memory)

  <driver>:
    fd              Unix file descriptor - open/lseek/read/write/close
                    (this is the default)
    stream          C file stream - fopen/fseeko/fread/fwrite/fclose

```

## Tests

### Driver comparison

Compare Unix file descriptor (fd) versus C stream-based i/o.  Create, truncate, and initialize jki.dat with data.  The default algorithm (jki_map) is used.

```
[frey@login01.darwin sapt-io-test]$ ./jki_to_jik -i jki.dat -I -d stream --n1=67 --n2=733 --n3=3146
INFO:  using i/o driver 'stream'
INFO:  init input file using algorithm 'jki_map'
INFO:  elapsed file init time 4.024314 s

[frey@login01.darwin sapt-io-test]$ ./jki_to_jik -i jki.dat -I -d fd --n1=67 --n2=733 --n3=3146
INFO:  using i/o driver 'fd'
INFO:  init input file using algorithm 'jki_map'
INFO:  elapsed file init time 93.829017 s
```

The buffering provided by the C stream library improves throughput significantly.  Recall the NVidia Fortran runtime uses C stream i/o while the Intel Fortran runtime uses file descriptors.  Building i/o-intensive SAPT program(s) with NVidia Fortran may significantly speed-up i/o.


#### Algorithm comparison

Using the Unix file descriptor i/o driver (longer wall times provide better statistics), the input file initialization is done and wall times compared:

```
[frey@login01.darwin sapt-io-test]$ for ALGORITHM in {jki,jik,ijk}_map vector_{input,output} matrix; do \
    rm -f jki.dat; \
    ./jki_to_jik -i jki.dat -I -d fd --n1=67 --n2=733 --n3=3146 -a $ALGORITHM | grep elapsed; \
done

INFO:  elapsed file init time 93.878460 s
INFO:  elapsed file init time 95.301265 s
INFO:  elapsed file init time 92.723298 s
INFO:  elapsed file init time 4.365485 s
INFO:  elapsed file init time 1.544764 s
INFO:  elapsed file init time 1.393603 s
```

As more memory is allocated to hold the data before writing to disk, the performance increases.  The same test sequence using the C stream-based i/o driver:

```
[frey@login01.darwin sapt-io-test]$ for ALGORITHM in {jki,jik,ijk}_map vector_{input,output} matrix; do \
    rm -f jki.dat; \
    ./jki_to_jik -i jki.dat -I -d stream --n1=67 --n2=733 --n3=3146 -a $ALGORITHM | grep elapsed; \
done

INFO:  elapsed file init time 4.283149 s
INFO:  elapsed file init time 4.247891 s
INFO:  elapsed file init time 4.381465 s
INFO:  elapsed file init time 2.092129 s
INFO:  elapsed file init time 1.816008 s
INFO:  elapsed file init time 1.288428 s
```

There is no longer as significant a performance spread because the C library is itself handling the in-memory data buffering that we explicitly implement with the vector and matrix algorithms.


### Data transpose

The actual work this code is performing is a transpose of indexed data.  The input file consists of a 4d tensor of dimension n1 x n2 x n3, correlating with indices (i, j, k).  The input file is indexed by j, k, and finally i:  the first word in the file is (1, 1, 1), the second is (1, 2, 1), up to (1, n2, 1) at which point k increments to (1,1,2).  The goal is to reorganize that data in another file, indexed by j, i, and finally k.



```
[frey@login01.darwin sapt-io-test]$ for ALGORITHM in vector_output matrix; do \
    rm -f jik.dat; \
    ./jki_to_jik -i jki.dat -d stream --n1=67 --n2=733 --n3=3146 -a $ALGORITHM -o jik.dat; \
    echo; \
done

INFO:  using i/o driver 'stream'
INFO:  input file open for reading: jki.dat
INFO:  (67, 733, 3146) data source is 1.15 GiB (1236025648 bytes)
INFO:  input file is 1.15 GiB (1236025648 bytes)
INFO:  output file open for writing: jik.dat
INFO:  using algorithm 'vector_output'
INFO:  write vector of size 24.58 KiB (25168 bytes) allocated
INFO:  elapsed file processing time 58.640828 s

INFO:  using i/o driver 'stream'
INFO:  input file open for reading: jki.dat
INFO:  (67, 733, 3146) data source is 1.15 GiB (1236025648 bytes)
INFO:  input file is 1.15 GiB (1236025648 bytes)
INFO:  output file open for writing: jik.dat
INFO:  using algorithm 'matrix'
INFO:  read+write matrices of size 2 x 1.61 MiB (1686256 bytes) allocated
INFO:  elapsed file processing time 1.391556 s
```
