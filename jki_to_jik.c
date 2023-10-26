
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <getopt.h>
#include <time.h>

//

typedef union {
    FILE        *stream;
    int         fd;
} file_handle_t;

typedef bool (*file_handle_open_t)(file_handle_t *fh, const char *path, bool read_only, bool should_create, bool should_trunc);
typedef bool (*file_handle_stat_t)(file_handle_t *fh, struct stat *finfo);
typedef off_t (*file_handle_seek_t)(file_handle_t *fh, off_t offset);
typedef ssize_t (*file_handle_read_t)(file_handle_t *fh, void *buffer, size_t buffer_len);
typedef ssize_t (*file_handle_write_t)(file_handle_t *fh, const void *buffer, size_t buffer_len);
typedef void (*file_handle_close_t)(file_handle_t *fh);

typedef struct {
    file_handle_open_t      open;
    file_handle_stat_t      stat;
    file_handle_seek_t      seek;
    file_handle_read_t      read;
    file_handle_write_t     write;
    file_handle_close_t     close;
} file_handle_callbacks;

//

bool
file_handle_open_fd(
    file_handle_t   *fh,
    const char      *path,
    bool            read_only,
    bool            should_create,
    bool            should_trunc
)
{
    va_list         vargs;
    int             rc, oflag = read_only ? O_RDONLY : O_RDWR;
    
    if ( should_create ) oflag |= O_CREAT;
    if ( should_trunc) oflag |= O_TRUNC;
    
    fh->fd = open(path, oflag, 0666);
    return (fh->fd >= 0) ? true : false;
}

bool
file_handle_stat_fd(
    file_handle_t   *fh,
    struct stat     *finfo
)
{
    return (fstat(fh->fd, finfo) == 0) ? true : false;
}

off_t
file_handle_seek_fd(
    file_handle_t   *fh,
    off_t           offset
)
{
    return lseek(fh->fd, offset, SEEK_SET);
}

ssize_t
file_handle_read_fd(
    file_handle_t   *fh,
    void            *buffer,
    size_t          buffer_len
)
{
    return read(fh->fd, buffer, buffer_len);
}

ssize_t
file_handle_write_fd(
    file_handle_t   *fh,
    const void      *buffer,
    size_t          buffer_len
)
{
    return write(fh->fd, buffer, buffer_len);
}

void
file_handle_close_fd(
    file_handle_t   *fh
)
{
    if ( fh->fd >= 0 ) {
        close(fh->fd);
        fh->fd = -1;
    }
}

static file_handle_callbacks file_handle_callbacks_fd = {
        file_handle_open_fd,
        file_handle_stat_fd,
        file_handle_seek_fd,
        file_handle_read_fd,
        file_handle_write_fd,
        file_handle_close_fd
    };

//

bool
file_handle_open_stream(
    file_handle_t   *fh,
    const char      *path,
    bool            read_only,
    bool            should_create,
    bool            should_trunc
)
{
    const char      *mode;
    
    if ( should_create ) {
        struct stat finfo;
        if ( stat(path, &finfo) == 0 ) {
            errno = EEXIST;
            return false;
        }
        read_only = false;
        // Fall through to below...
    }
    if ( read_only ) {
        fh->stream = fopen(path, "rb");
    } else {
        fh->stream = fopen(path, "wb+");
        if ( fh->stream && should_trunc ) ftruncate(fileno(fh->stream), 0);
    }
    return fh->stream ? true : false;
}

bool
file_handle_stat_stream(
    file_handle_t   *fh,
    struct stat     *finfo
)
{
    return (fstat(fileno(fh->stream), finfo) == 0) ? true : false;
}

off_t
file_handle_seek_stream(
    file_handle_t   *fh,
    off_t           offset
)
{
    if ( fseeko(fh->stream, offset, SEEK_SET) == 0 ) return ftello(fh->stream);
    return -1;
}

ssize_t
file_handle_read_stream(
    file_handle_t   *fh,
    void            *buffer,
    size_t          buffer_len
)
{
    size_t          n_items = fread(buffer, buffer_len, 1, fh->stream);
    
    if ( n_items == 1 ) return buffer_len;
    if ( feof(fh->stream) ) return 0;
    return -1;
}

ssize_t
file_handle_write_stream(
    file_handle_t   *fh,
    const void      *buffer,
    size_t          buffer_len
)
{
    size_t          n_items = fwrite(buffer, buffer_len, 1, fh->stream);
    
    if ( n_items == 1 ) return buffer_len;
    if ( feof(fh->stream) ) return 0;
    return -1;
}

void
file_handle_close_stream(
    file_handle_t   *fh
)
{
    if ( fh->stream >= 0 ) {
        fclose(fh->stream);
        fh->stream = NULL;
    }
}

static file_handle_callbacks file_handle_callbacks_stream = {
        file_handle_open_stream,
        file_handle_stat_stream,
        file_handle_seek_stream,
        file_handle_read_stream,
        file_handle_write_stream,
        file_handle_close_stream
    };

//

typedef enum {
    algorithm_invalid = -1,
    algorithm_ijk_map = 0,
    algorithm_jki_map,
    algorithm_jik_map,
    algorithm_vector_input,
    algorithm_vector_output,
    algorithm_matrix,
    algorithm_max
} algorithm_t;

static char const* algorithm_names[] = {
        "ijk_map",
        "jki_map",
        "jik_map",
        "vector_input",
        "vector_output",
        "matrix",
        NULL
    };

algorithm_t
string_to_algorithm(
    const char  *s
)
{
    int         a = 0;
    
    while ( algorithm_names[a] ) {
        if ( strcasecmp(algorithm_names[a], s) == 0 ) return a;
        a++;
    }
    return algorithm_invalid;
}

//

typedef enum {
    io_driver_invalid = -1,
    io_driver_fd = 0,
    io_driver_stream,
    io_driver_max
} io_driver_t;

static char const* io_driver_names[] = {
        "fd",
        "stream",
        NULL
    };

static file_handle_callbacks* io_driver_callbacks[] = {
        &file_handle_callbacks_fd,
        &file_handle_callbacks_stream,
        NULL
    };

io_driver_t
string_to_io_driver(
    const char  *s
)
{
    int         d = 0;
    
    while ( io_driver_names[d] ) {
        if ( strcasecmp(io_driver_names[d], s) == 0 ) return d;
        d++;
    }
    return io_driver_invalid;
}

//

static struct option cli_options[] = {
        { "help",       no_argument,       0, 'h' },
        { "input",      required_argument, 0, 'i' },
        { "output",     required_argument, 0, 'o' },
        { "n1",         required_argument, 0, '1' },
        { "n2",         required_argument, 0, '2' },
        { "n3",         required_argument, 0, '3' },
        { "exact-dims", no_argument,       0, 'x' },
        { "algorithm",  required_argument, 0, 'a' },
        { "io-driver",  required_argument, 0, 'd' },
        { "init-input", no_argument,       0, 'I' },
        { NULL, 0, 0, 0 }
    };
static char *cli_options_str = "hi:o:1:2:3:xa:d:I";

void
usage(
    const char  *exe
)
{
    printf(
            "usage:\n\n"
            "    %s {options}\n\n"
            "  options:\n\n"
            "    -h/--help                    show this information\n"
            "    -1 #, --n1=#                 range of index i\n"
            "    -2 #, --n2=#                 range of index j\n"
            "    -3 #, --n3=#                 range of index k\n"
            "    -i <filepath>,               read (or possibly init) this file\n"
            "        --input=<filepath>         as the source\n"
            "    -o <filepath>,               write this file as the destination\n"
            "        --output=<filepath>\n"
            "    -x, --exact-dims             file sizes must exactly match the\n"
            "                                   n1/n2/n3 dimensions\n"
            "    -a <algorithm>,              use this specific i/o algorithm\n"
            "        --algorithm=<algorithm>    in the input init and file processing\n"
            "    -d <driver>,                 use this specific i/o driver for all\n"
            "        --driver=<driver>          file access\n"
            "    -I, --init-input             generate newly-initialized data in\n"
            "                                   in the input file\n\n"
            "  <algorithm>:\n"
            "    jki_map         iterates in sequence j, k, i, reading from input\n"
            "                    then writing to output (this is the default)\n" 
            "    jik_map         iterates in sequence j, i, k, reading from input\n"
            "                    then writing to output\n"
            "    ijk_map         iterates in sequence i, j, k, reading from input\n"
            "                    then writing to output\n"
            "    vector_input    1xn1 chunks are read from input then mapped by\n"
            "                    index iteration to the output (requires n3 words of\n"
            "                    memory)\n"
            "    vector_output   1xn3 chunks are mapped by index iteration from the\n"
            "                    input then written to the output (requires n3 words\n"
            "                    of memory)\n"
            "    matrix          n1xn3 chunks are read from input then transposed\n"
            "                    in memory and written en masse to the output\n"
            "                    (requires 2 x n1 x n3 words of memory)\n\n"
            "  <driver>:\n"
            "    fd              Unix file descriptor - open/lseek/read/write/close\n"
            "                    (this is the default)\n"
            "    stream          C file stream - fopen/fseeko/fread/fwrite/fclose\n"
            "\n",
            exe);
}

//

unsigned long
offset_ijk(
    unsigned long   *n,
    unsigned long   i,
    unsigned long   j,
    unsigned long   k
)
{
    return i * n[2] * n[1] + n[2] * j + k;
}

//

unsigned long
offset_jki(
    unsigned long   *n,
    unsigned long   i,
    unsigned long   j,
    unsigned long   k
)
{
    return j * n[0] * n[2] + n[0] * k + i;
}

//

unsigned long
offset_jik(
    unsigned long   *n,
    unsigned long   i,
    unsigned long   j,
    unsigned long   k
)
{
    return j * n[0] * n[2] + n[2] * i + k;
}

//

const char*
memory_with_natural_unit(
    size_t  bytes
)
{
    static char*    prefix[] = {
                            "",
                            "Ki",
                            "Mi",
                            "Gi",
                            "Ti",
                            "Pi"
                        };
    static char     memstr[256];
    int             prefix_idx = 0, prefix_max = (sizeof(prefix) / sizeof(char*)) - 1;
    double          value = (double)bytes;
    
    while ( (value > 1024.0) && (prefix_idx < prefix_max) ) value /= 1024.0, prefix_idx++;
    if ( prefix_idx > 0 ) {
        prefix_max = snprintf(memstr, sizeof(memstr), "%.2lf %sB (%llu bytes)", value, prefix[prefix_idx], (unsigned long long)bytes);
    } else {
        prefix_max = snprintf(memstr, sizeof(memstr), "%llu B", (unsigned long long)bytes);
    }
    if ( prefix_max >= sizeof(memstr) ) {
        fprintf(stderr, "CRITICAL:  memstr buffer too small in memory_with_natural_unit\n");
        exit(ENOMEM);
    }
    return memstr;
}

//

int
main(
    int       argc,
    char*     argv[]
)
{
    int                     opt_char, rc = 0;
    const char              *input_file = NULL, *output_file = NULL;
    file_handle_t           in_fh, out_fh;
    io_driver_t             use_io_driver = io_driver_fd;
    file_handle_callbacks   *io_driver;
    bool                    should_use_exact_dims = false;
    algorithm_t             use_algorithm = algorithm_jki_map;
    bool                    should_init_input = false;
    unsigned long           i, j, k, n[3] = { 0, 0, 0 };
    size_t                  l;
    struct stat             finfo;
    struct timespec         timer[2];
    double                  dt;
    
    //
    // Process CLI options:
    //
    while ( (opt_char = getopt_long(argc, argv, cli_options_str, cli_options, NULL)) != -1 ) {
        switch ( opt_char ) {
            case 'h':
                usage(argv[0]);
                exit(0);
        
            case 'a':
                if ( optarg && *optarg ) {
                    algorithm_t     a = string_to_algorithm(optarg);
                    
                    if ( a == algorithm_invalid ) {
                        fprintf(stderr, "ERROR:  invalid algorithm name: %s\n", optarg);
                        exit(EINVAL);
                    }
                    use_algorithm = a;
                } else {
                    fprintf(stderr, "ERROR:  invalid algorithm name\n");
                    exit(EINVAL);
                }
                break;
        
            case 'd':
                if ( optarg && *optarg ) {
                    io_driver_t     d = string_to_io_driver(optarg);
                    
                    if ( d == io_driver_invalid ) {
                        fprintf(stderr, "ERROR:  invalid i/o driver name: %s\n", optarg);
                        exit(EINVAL);
                    }
                    use_io_driver = d;
                } else {
                    fprintf(stderr, "ERROR:  invalid i/o driver name\n");
                    exit(EINVAL);
                }
                break;
        
            case 'I':
                should_init_input = true;
                break;
        
            case 'x':
                should_use_exact_dims = true;
                break;
                
            case 'i':
                if ( optarg && *optarg ) {
                    input_file = (const char*)optarg;
                } else {
                    fprintf(stderr, "ERROR:  invalid input file name\n");
                    exit(EINVAL);
                }
                break;
                
            case 'o':
                if ( optarg && *optarg ) {
                    output_file = (const char*)optarg;
                } else {
                    fprintf(stderr, "ERROR:  invalid output file name\n");
                    exit(EINVAL);
                }
                break;
            
            case '1':
            case '2':
            case '3': {
                if ( optarg && *optarg ) {
                    char            *eos = NULL;
                    unsigned long   v = strtoul(optarg, &eos, 0);
                    
                    if ( v && (eos > optarg) ) {
                        n[opt_char - '1'] = v;
                    } else {
                        fprintf(stderr, "ERROR:  invalid dimension n%c: %s\n", opt_char, optarg);
                        exit(EINVAL);
                    }
                } else {
                    fprintf(stderr, "ERROR:  invalid dimension n%c\n", opt_char);
                    exit(EINVAL);
                }
                break;
            }
                
        }
    }
    
    //
    // Chooose the i/o driver:
    //
    io_driver = io_driver_callbacks[use_io_driver];
    printf("INFO:  using i/o driver '%s'\n", io_driver_names[use_io_driver]);
    
    //
    // Validate all dimensions provided:
    //
    for ( i=0; i < 3; i++ ) {
        if ( n[i] == 0 ) {
            fprintf(stderr, "ERROR:  invalid dimension n%lu: 0\n", (i + 1));
            exit(EINVAL);
        }
    }
    
    //
    // Validate input file name provided:
    //
    if ( ! input_file ) {
        fprintf(stderr, "ERROR:  no input file name provided\n");
        exit(EINVAL);
    }
    
    //
    // Initialize the input file?
    //
    if ( should_init_input ) {
        if ( ! io_driver->open(&in_fh, input_file, false, true, true) ) {
            if ( errno != EEXIST ) {
                fprintf(stderr, "ERROR:  unable to create input file (errno = %d)\n", errno);
                exit(errno);
            }
            if ( ! io_driver->open(&in_fh, input_file, false, false, true) ) {
                fprintf(stderr, "ERROR:  unable to truncate input file (errno = %d)\n", errno);
                exit(errno);
            }
        }    
        printf("INFO:  init input file using algorithm '%s'\n", algorithm_names[use_algorithm]);
    
        clock_gettime(CLOCK_MONOTONIC, &timer[0]);
    
        switch ( use_algorithm ) {
    
            case algorithm_invalid:
            case algorithm_max:
                break;
            
            case algorithm_ijk_map: {
                for ( i=0; i<n[0]; i++ ) {
                    for ( j=0; j<n[1]; j++ ) {
                        for ( k=0; k<n[2]; k++ ) {
                            ssize_t n_bytes;
                            
                            double v = offset_ijk(n, i, j, k);
                            n_bytes = io_driver->write(&in_fh, &v, sizeof(v));
                            if ( n_bytes <= 0 ) {
                                fprintf(stderr, "ERROR:  unable to write (%lu, %lu, %lu) to input file (errno = %d)\n", i, j, k, errno);
                                exit(errno);
                            }
                        }
                    }
                }
                break;
            }
            
            case algorithm_jki_map: {
                for ( j=0; j<n[1]; j++ ) {
                    for ( k=0; k<n[2]; k++ ) {
                        for ( i=0; i<n[0]; i++ ) {
                            ssize_t n_bytes;
                            
                            double v = offset_jki(n, i, j, k);
                            n_bytes = io_driver->write(&in_fh, &v, sizeof(v));
                            if ( n_bytes <= 0 ) {
                                fprintf(stderr, "ERROR:  unable to write (%lu, %lu, %lu) to input file (errno = %d)\n", i, j, k, errno);
                                exit(errno);
                            }
                        }
                    }
                }
                break;
            }
            
            case algorithm_jik_map: {
                for ( i=0; i<n[0]; i++ ) {
                    for ( j=0; j<n[1]; j++ ) {
                        for ( k=0; k<n[2]; k++ ) {
                            ssize_t n_bytes;
                            
                            double v = offset_jik(n, i, j, k);
                            n_bytes = io_driver->write(&in_fh, &v, sizeof(v));
                            if ( n_bytes <= 0 ) {
                                fprintf(stderr, "ERROR:  unable to write (%lu, %lu, %lu) to input file (errno = %d)\n", i, j, k, errno);
                                exit(errno);
                            }
                        }
                    }
                }
                break;
            }
            
            case algorithm_vector_input: {
                size_t      v_len = sizeof(double) * n[0];
                double      *v = (double*)malloc(v_len);
                    
                if ( ! v ) {
                    fprintf(stderr, "ERROR:  unable to allocate init read vector in vector_input\n");
                    exit(ENOMEM);
                }
                printf("INFO:  init read vector of size %s allocated\n", memory_with_natural_unit(v_len));
                
                for ( j=0; j<n[1]; j++ ) {
                    for ( k=0; k<n[2]; k++ ) {
                        ssize_t n_bytes;
                        
                        for ( i=0; i<n[0]; i++ ) v[i] = offset_jki(n, i, j, k);
                        n_bytes = io_driver->write(&in_fh, v, v_len);
                    }
                }
                free((void*)v);
                break;
            }
            
            case algorithm_vector_output: {
                size_t      v_len = sizeof(double) * n[2];
                double      *v = (double*)malloc(v_len);
                    
                if ( ! v ) {
                    fprintf(stderr, "ERROR:  unable to allocate init write vector in vector_input\n");
                    exit(ENOMEM);
                }
                printf("INFO:  init write vector of size %s allocated\n", memory_with_natural_unit(v_len));
                
                for ( j=0; j<n[1]; j++ ) {
                    for ( i=0; i<n[0]; i++ ) {
                        ssize_t n_bytes;
                        
                        for ( k=0; k<n[2]; k++ ) v[k] = offset_jki(n, i, j, k);
                        n_bytes = io_driver->write(&in_fh, v, v_len);
                    }
                }
                free((void*)v);
                break;
            }
            
            case algorithm_matrix: {
                size_t      v_len = sizeof(double) * n[0] * n[2];
                double      *v = (double*)malloc(v_len);
                    
                if ( ! v ) {
                    fprintf(stderr, "ERROR:  unable to allocate init read+write matrix in matrix\n");
                    exit(ENOMEM);
                }
                printf("INFO:  init read+write matrix of size %s allocated\n", memory_with_natural_unit(v_len));
            
                for ( j=0; j<n[1]; j++ ) {
                    ssize_t n_bytes;
                    
                    for ( k=0; k<n[2]; k++ ) {
                        for ( i=0; i<n[0]; i++ ) {
                            v[n[0] * k + i] = offset_jki(n, i, j, k);
                        }
                    }
                    n_bytes = io_driver->write(&in_fh, v, v_len);
                }
                free((void*)v);
                break;
            }
            
        }
        io_driver->close(&in_fh);
        clock_gettime(CLOCK_MONOTONIC, &timer[1]);
        dt = (timer[1].tv_sec - timer[0].tv_sec) + 1e-9 * (timer[1].tv_nsec - timer[0].tv_nsec);
    
        printf("INFO:  elapsed file init time %.6lf s\n", dt); 
        if ( ! output_file ) exit(0);   
    }
    
    //
    // Validate output file name provided:
    //
    if ( ! output_file ) {
        fprintf(stderr, "ERROR:  no output file name provided\n");
        exit(EINVAL);
    }
    
    //
    // Get the input file opened:
    //
    if ( ! io_driver->open(&in_fh, input_file, true, false, false) ) {
        fprintf(stderr, "ERROR:  unable to open input file for reading (errno = %d)\n", errno);
        exit(errno);
    }
    printf("INFO:  input file open for reading: %s\n", input_file);
    
    //
    // Check the size of the input file:
    //
    if ( ! io_driver->stat(&in_fh, &finfo) ) {
        fprintf(stderr, "ERROR:  unable to get metadata for input file (errno = %d)\n", errno);
        exit(errno);
    }
    // Anticipated size of data:
    l = sizeof(double) * n[0] * n[1] * n[2];
    if ( finfo.st_size < l ) {
        fprintf(stderr, "ERROR:  input file is too small for dimensions (%lu, %lu, %lu): %lld\n", n[0], n[1], n[2], finfo.st_size);
        exit(EINVAL);
    }
    if ( (finfo.st_size > l) && should_use_exact_dims ) {
        fprintf(stderr, "ERROR:  input file is too large for dimensions (%lu, %lu, %lu): %lld\n", n[0], n[1], n[2], finfo.st_size);
        exit(EINVAL);
    }
    printf("INFO:  (%lu, %lu, %lu) data source is %s\n"
           "INFO:  input file is %s\n",
           n[0], n[1], n[2], memory_with_natural_unit((size_t)l), memory_with_natural_unit((size_t)finfo.st_size));
    
    //
    // Try to create the output file:
    //
    if ( ! io_driver->open(&out_fh, output_file, false, true, false) ) {
        if ( errno != EEXIST ) {
            fprintf(stderr, "ERROR:  unable to create output file (errno = %d)\n", errno);
            exit(errno);
        }
        //
        // The file already exists, so get it opened w/o asking to create it:
        //
        if ( ! io_driver->open(&out_fh, output_file, false, false, false) ) {
            fprintf(stderr, "ERROR:  unable to open output file (errno = %d)\n", errno);
            exit(errno);
        }
        
        //
        // Check the size of the output file:
        //
        if ( ! io_driver->stat(&out_fh, &finfo) ) {
            fprintf(stderr, "ERROR:  unable to get metadata for output file (errno = %d)\n", errno);
            exit(errno);
        }
        if ( finfo.st_size < l ) {
            fprintf(stderr, "ERROR:  output file is too small for dimensions (%lu, %lu, %lu): %lld\n", n[0], n[1], n[2], finfo.st_size);
            exit(EINVAL);
        }
        if ( (finfo.st_size > l) && should_use_exact_dims ) {
            fprintf(stderr, "ERROR:  output file is too large for dimensions (%lu, %lu, %lu): %lld\n", n[0], n[1], n[2], finfo.st_size);
            exit(EINVAL);
        }
        printf("INFO:  (%lu, %lu, %lu) data source is %s\n"
               "INFO:  output file is %s\n",
               n[0], n[1], n[2], memory_with_natural_unit((size_t)l), memory_with_natural_unit((size_t)finfo.st_size));
        
    }
    printf("INFO:  output file open for writing: %s\n", output_file);
    
    printf("INFO:  using algorithm '%s'\n", algorithm_names[use_algorithm]);
    
    clock_gettime(CLOCK_MONOTONIC, &timer[0]);
    
    switch ( use_algorithm ) {
    
        case algorithm_invalid:
        case algorithm_max:
            break;
            
        case algorithm_ijk_map: {
            for ( i=0; i<n[0]; i++ ) {
                for ( j=0; j<n[1]; j++ ) {
                    for ( k=0; k<n[2]; k++ ) {
                        ssize_t     n_bytes;
                        double      v;
                        off_t       fp = sizeof(double) * offset_jki(n, i, j, k);
                        
                        if ( io_driver->seek(&in_fh, fp) < 0 ) {
                            fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, %lu) = %lld in input file (errno = %d)\n", i, j, k, fp, errno);
                            exit(errno);
                        }
                        n_bytes = io_driver->read(&in_fh, &v, sizeof(v));
                        if ( n_bytes <= 0 ) {
                            if ( n_bytes == 0 ) {
                                fprintf(stderr, "ERROR:  unexpected end-of-file on input file\n");
                                exit(EINVAL);
                            }
                            fprintf(stderr, "ERROR:  unable to read (%lu, %lu, %lu) from input file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                        fp = sizeof(double) * offset_jik(n, i, j, k);
                        
                        if ( io_driver->seek(&out_fh, fp) < 0 ) {
                            fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, %lu) in output file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                        n_bytes = io_driver->write(&out_fh, &v, sizeof(v));
                        if ( n_bytes <= 0 ) {
                            fprintf(stderr, "ERROR:  unable to write (%lu, %lu, %lu) to output file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                    }
                }
            }
            break;
        }
            
        case algorithm_jki_map: {
            for ( j=0; j<n[1]; j++ ) {
                for ( k=0; k<n[2]; k++ ) {
                    for ( i=0; i<n[0]; i++ ) {
                        ssize_t     n_bytes;
                        double      v;
                        off_t       fp = sizeof(double) * offset_jki(n, i, j, k);
                        
                        if ( io_driver->seek(&in_fh, fp) < 0 ) {
                            fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, %lu) = %lld in input file (errno = %d)\n", i, j, k, fp, errno);
                            exit(errno);
                        }
                        n_bytes = io_driver->read(&in_fh, &v, sizeof(v));
                        if ( n_bytes <= 0 ) {
                            if ( n_bytes == 0 ) {
                                fprintf(stderr, "ERROR:  unexpected end-of-file on input file\n");
                                exit(EINVAL);
                            }
                            fprintf(stderr, "ERROR:  unable to read (%lu, %lu, %lu) from input file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                        fp = sizeof(double) * offset_jik(n, i, j, k);
                        
                        if ( io_driver->seek(&out_fh, fp) < 0 ) {
                            fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, %lu) in output file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                        n_bytes = io_driver->write(&out_fh, &v, sizeof(v));
                        if ( n_bytes <= 0 ) {
                            fprintf(stderr, "ERROR:  unable to write (%lu, %lu, %lu) to output file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                    }
                }
            }
            break;
        }
            
        case algorithm_jik_map: {
            for ( j=0; j<n[1]; j++ ) {
                for ( i=0; i<n[0]; i++ ) {
                    for ( k=0; k<n[2]; k++ ) {
                        ssize_t     n_bytes;
                        double      v;
                        off_t       fp = sizeof(double) * offset_jki(n, i, j, k);
                        
                        if ( io_driver->seek(&in_fh, fp) < 0 ) {
                            fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, %lu) = %lld in input file (errno = %d)\n", i, j, k, fp, errno);
                            exit(errno);
                        }
                        n_bytes = io_driver->read(&in_fh, &v, sizeof(v));
                        if ( n_bytes <= 0 ) {
                            if ( n_bytes == 0 ) {
                                fprintf(stderr, "ERROR:  unexpected end-of-file on input file\n");
                                exit(EINVAL);
                            }
                            fprintf(stderr, "ERROR:  unable to read (%lu, %lu, %lu) from input file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                        fp = sizeof(double) * offset_jik(n, i, j, k);
                        
                        if ( io_driver->seek(&out_fh, fp) < 0 ) {
                            fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, %lu) in output file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                        n_bytes = io_driver->write(&out_fh, &v, sizeof(v));
                        if ( n_bytes <= 0 ) {
                            fprintf(stderr, "ERROR:  unable to write (%lu, %lu, %lu) to output file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                    }
                }
            }
            break;
        }
        
        case algorithm_vector_input: {
            size_t      v_len = sizeof(double) * n[0];
            double      *v = (double*)malloc(v_len);
                    
            if ( ! v ) {
                fprintf(stderr, "ERROR:  unable to allocate read vector in vector_input\n");
                exit(ENOMEM);
            }
            printf("INFO:  read vector of size %s allocated\n", memory_with_natural_unit(v_len));
            
            for ( j=0; j<n[1]; j++ ) {
                for ( k=0; k<n[2]; k++ ) {
                    ssize_t     n_bytes;
                    off_t       fp = sizeof(double) * offset_jki(n, 0, j, k);
                    
                    if ( io_driver->seek(&in_fh, fp) < 0 ) {
                        fprintf(stderr, "ERROR:  unable to seek to (..., %lu, %lu) = %lld in input file (errno = %d)\n", j, k, fp, errno);
                        exit(errno);
                    }
                    n_bytes = io_driver->read(&in_fh, v, v_len);
                    if ( n_bytes <= 0 ) {
                        if ( n_bytes == 0 ) {
                            fprintf(stderr, "ERROR:  unexpected end-of-file on input file\n");
                            exit(EINVAL);
                        }
                        fprintf(stderr, "ERROR:  unable to read (..., %lu, %lu) from input file (errno = %d)\n", j, k, errno);
                        exit(errno);
                    }
                    for ( i=0; i<n[0]; i++ ) {
                        fp = sizeof(double) * offset_jik(n, i, j, k);
                    
                        if ( io_driver->seek(&out_fh, fp) < 0 ) {
                            fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, %lu) in output file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                        n_bytes = io_driver->write(&out_fh, v + i, sizeof(double));
                        if ( n_bytes <= 0 ) {
                            fprintf(stderr, "ERROR:  unable to write (%lu, %lu, %lu) to output file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                    }
                }
            }
            free((void*)v);
            break;
        }
        
        case algorithm_vector_output: {
            size_t      v_len = sizeof(double) * n[2];
            double      *v = (double*)malloc(v_len);
                    
            if ( ! v ) {
                fprintf(stderr, "ERROR:  unable to allocate write vector in vector_output\n");
                exit(ENOMEM);
            }
            printf("INFO:  write vector of size %s allocated\n", memory_with_natural_unit(v_len));
            
            for ( j=0; j<n[1]; j++ ) {
                for ( i=0; i<n[0]; i++ ) {
                    off_t           fp;
                    ssize_t         n_bytes;
                    
                    for ( k=0; k<n[2]; k++ ) {
                        fp = sizeof(double) * offset_jki(n, i, j, k);
                        if ( io_driver->seek(&in_fh, fp) < 0 ) {
                            fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, %lu) = %lld in input file (errno = %d)\n", i, j, k, fp, errno);
                            exit(errno);
                        }
                        n_bytes = io_driver->read(&in_fh, v + k, sizeof(double));
                        if ( n_bytes <= 0 ) {
                            if ( n_bytes == 0 ) {
                                fprintf(stderr, "ERROR:  unexpected end-of-file on input file\n");
                                exit(EINVAL);
                            }
                            fprintf(stderr, "ERROR:  unable to read (%lu, %lu, %lu) from input file (errno = %d)\n", i, j, k, errno);
                            exit(errno);
                        }
                    }
                    
                    fp = sizeof(double) * offset_jik(n, i, j, 0);
                    
                    if ( io_driver->seek(&out_fh, fp) < 0 ) {
                        fprintf(stderr, "ERROR:  unable to seek to (%lu, %lu, ...) in output file (errno = %d)\n", i, j, errno);
                        exit(errno);
                    }
                    n_bytes = io_driver->write(&out_fh, v, v_len);
                    if ( n_bytes <= 0 ) {
                        fprintf(stderr, "ERROR:  unable to write (%lu, %lu, ...) to output file (errno = %d)\n", i, j, errno);
                        exit(errno);
                    }
                }
            }
            free((void*)v);
            break;
        }
        
        case algorithm_matrix: {
            size_t      v_len = sizeof(double) * n[0] * n[2];
            double      *v1 = (double*)malloc(2 * v_len);
            double      *v2;
                    
            if ( ! v1 ) {
                fprintf(stderr, "ERROR:  unable to allocate read+write matrices in matrix\n");
                exit(ENOMEM);
            }
            printf("INFO:  read+write matrices of size 2 x %s allocated\n", memory_with_natural_unit(v_len));
            v2 = v1 + n[0] * n[2];
            
            for ( j=0; j<n[1]; j++ ) {
                ssize_t     n_bytes;
                off_t       fp = sizeof(double) * offset_jki(n, 0, j, 0);
                
                if ( io_driver->seek(&in_fh, fp) < 0 ) {
                    fprintf(stderr, "ERROR:  unable to seek to (..., %lu, ...) = %lld in input file (errno = %d)\n", j, fp, errno);
                    exit(errno);
                }
                n_bytes = io_driver->read(&in_fh, v1, v_len);
                if ( n_bytes <= 0 ) {
                    if ( n_bytes == 0 ) {
                        fprintf(stderr, "ERROR:  unexpected end-of-file on input file\n");
                        exit(EINVAL);
                    }
                    fprintf(stderr, "ERROR:  unable to read (..., %lu, ...) from input file (errno = %d)\n", j, errno);
                    exit(errno);
                }
                for ( i=0; i<n[0]; i++ ) {
                    for ( k=0; k<n[2]; k++ ) {
                        v2[i * n[2] + k] = v1[k * n[0] + i];
                    }
                }
                fp = sizeof(double) * offset_jik(n, 0, j, 0);
            
                if ( io_driver->seek(&out_fh, fp) < 0 ) {
                    fprintf(stderr, "ERROR:  unable to seek to (..., %lu, ...) in output file (errno = %d)\n", j, errno);
                    exit(errno);
                }
                n_bytes = io_driver->write(&out_fh, v2, v_len);
                if ( n_bytes <= 0 ) {
                    fprintf(stderr, "ERROR:  unable to write (..., %lu, ...) to output file (errno = %d)\n", j, errno);
                    exit(errno);
                }
            }
            free((void*)v1);
            break;
        }
    
    }
    io_driver->close(&out_fh);
    clock_gettime(CLOCK_MONOTONIC, &timer[1]);
    dt = (timer[1].tv_sec - timer[0].tv_sec) + 1e-9 * (timer[1].tv_nsec - timer[0].tv_nsec);
    
    printf("INFO:  elapsed file processing time %.6lf s\n", dt);
    
    io_driver->close(&in_fh);
    return rc;
}
