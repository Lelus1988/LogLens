#define _POSIX_C_SOURCE 200809L // NOLINT
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#define isatty _isatty
typedef __int64 file_offset_t;
#define file_tell _ftelli64
#define file_seek _fseeki64
#else
#include <dirent.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
typedef off_t file_offset_t;
#define file_tell ftello
#define file_seek fseeko
#endif

#ifdef _WIN32
typedef struct _stat stat_t;
#define stat _stat
#else
typedef struct stat stat_t;
#endif

static FILE *open_input(const char *path)
{
#ifdef _WIN32
    HANDLE handle = CreateFileA(path, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = EACCES;
        return NULL;
    }
    int descriptor = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
    if (descriptor == -1) {
        CloseHandle(handle);
        return NULL;
    }
    FILE *file = _fdopen(descriptor, "rb");
    if (file == NULL) {
        _close(descriptor);
    }
    return file;
#else
    return fopen(path, "rb");
#endif
}

typedef enum {
    LEVEL_ALL,
    LEVEL_ERROR,
    LEVEL_WARNING,
    LEVEL_INFO,
    LEVEL_DEBUG,
    LEVEL_OTHER
} LogLevel;

typedef struct {
    const char *path;
    const char *search;
    const char *export_path;
    LogLevel level;
    int follow;
    int color;
    int no_color;
    int show_source;
} Options;

#define PATH_BUFFER_SIZE 32768

static void print_usage(FILE *stream)
{
    fprintf(stream,
            "Usage: loglens [options] [<logfile-or-folder>]\n"
            "\n"
            "When no path is given, Windows opens the file picker; other terminals\n"
            "prompt for a path.\n"
            "Options:\n"
            "  -s, --search TEXT     Show only lines containing TEXT (case-insensitive)\n"
            "  -l, --level LEVEL     Filter by error, warning, info, debug, other, or all\n"
            "  -f, --follow          Continue showing lines as the file grows\n"
            "  -e, --export FILE     Write matching lines to FILE\n"
            "      --no-color        Disable colored output\n"
            "  -h, --help            Show this help\n");
}

static int parse_level(const char *text, LogLevel *level)
{
    if (strcmp(text, "all") == 0) {
        *level = LEVEL_ALL;
    } else if (strcmp(text, "error") == 0) {
        *level = LEVEL_ERROR;
    } else if (strcmp(text, "warning") == 0 || strcmp(text, "warn") == 0) {
        *level = LEVEL_WARNING;
    } else if (strcmp(text, "info") == 0) {
        *level = LEVEL_INFO;
    } else if (strcmp(text, "debug") == 0 || strcmp(text, "trace") == 0) {
        *level = LEVEL_DEBUG;
    } else if (strcmp(text, "other") == 0) {
        *level = LEVEL_OTHER;
    } else {
        return 0;
    }
    return 1;
}

static int parse_options(int argc, char **argv, Options *options)
{
    memset(options, 0, sizeof(*options));
    options->level = LEVEL_ALL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(stdout);
            return 2;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            options->no_color = 1;
        } else if (strcmp(argv[i], "--follow") == 0 || strcmp(argv[i], "-f") == 0) {
            options->follow = 1;
        } else if (strcmp(argv[i], "--search") == 0 || strcmp(argv[i], "-s") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "loglens: --search requires text\n");
                return 0;
            }
            options->search = argv[i];
        } else if (strcmp(argv[i], "--level") == 0 || strcmp(argv[i], "-l") == 0) {
            if (++i >= argc || !parse_level(argv[i], &options->level)) {
                fprintf(stderr, "loglens: --level must be error, warning, info, debug, other, or all\n");
                return 0;
            }
        } else if (strcmp(argv[i], "--export") == 0 || strcmp(argv[i], "-e") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "loglens: --export requires a file path\n");
                return 0;
            }
            options->export_path = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "loglens: unknown option: %s\n", argv[i]);
            return 0;
        } else if (options->path != NULL) {
            fprintf(stderr, "loglens: only one input log file can be specified\n");
            return 0;
        } else {
            options->path = argv[i];
        }
    }

    return 1;
}

static int prompt_for_path(char *path, size_t path_size)
{
#ifdef _WIN32
    HANDLE input_handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD original_input_mode; // NOLINT
    int have_console = input_handle != INVALID_HANDLE_VALUE &&
                       GetConsoleMode(input_handle, &original_input_mode);
    if (have_console) {
        OPENFILENAMEA dialog = {0};
        path[0] = '\0';
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = GetConsoleWindow();
        dialog.lpstrFile = path;
        dialog.nMaxFile = (DWORD)path_size;
        dialog.lpstrFilter = "Log and text files\0*.log;*.txt;*.out;*.err\0All files\0*.*\0";
        dialog.nFilterIndex = 2;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&dialog)) {
            return 1;
        }
        if (CommDlgExtendedError() != 0) {
            fprintf(stderr, "loglens: file picker failed (error %lu)\n",
                    (unsigned long)CommDlgExtendedError());
            return 0;
        }
    }
    {
        char input[PATH_BUFFER_SIZE];
        printf("Enter file or folder path: ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            fprintf(stderr, "loglens: no path was selected or entered\n");
            return 0;
        }
        input[strcspn(input, "\r\n")] = '\0';
        if (input[0] == '\0') {
            fprintf(stderr, "loglens: no path was entered\n");
            return 0;
        }
        size_t input_length = strlen(input);
        if (input_length >= path_size) {
            fprintf(stderr, "loglens: path is too long\n");
            return 0;
        }
        memcpy(path, input, input_length + 1);
        return 1;
    }
#else
    char input[PATH_BUFFER_SIZE];
    printf("LogLens - enter a file or folder path: ");
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) == NULL) {
        fprintf(stderr, "loglens: no path was entered\n");
        return 0;
    }
    input[strcspn(input, "\r\n")] = '\0';
    if (input[0] == '\0') {
        fprintf(stderr, "loglens: no path was entered\n");
        return 0;
    }
    size_t input_length = strlen(input);
    if (input_length >= path_size) {
        fprintf(stderr, "loglens: path is too long\n");
        return 0;
    }
    memcpy(path, input, input_length + 1);
    return 1;
#endif
}

static int contains_case_insensitive(const char *text, const char *needle)
{
    const unsigned char *start = (const unsigned char *)text;
    size_t needle_length = strlen(needle);

    if (needle_length == 0) {
        return 1;
    }
    while (*start != '\0') {
        size_t i = 0;
        while (i < needle_length && start[i] != '\0' &&
               tolower(start[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == needle_length) {
            return 1;
        }
        ++start;
    }
    return 0;
}

static LogLevel classify_line(const char *line)
{
    if (contains_case_insensitive(line, "fatal") ||
        contains_case_insensitive(line, "error") ||
        contains_case_insensitive(line, "err:") ||
        contains_case_insensitive(line, "err ")) {
        return LEVEL_ERROR;
    }
    if (contains_case_insensitive(line, "warning") ||
        contains_case_insensitive(line, "warn:") ||
        contains_case_insensitive(line, "warn ")) {
        return LEVEL_WARNING;
    }
    if (contains_case_insensitive(line, "debug") ||
        contains_case_insensitive(line, "trace")) {
        return LEVEL_DEBUG;
    }
    if (contains_case_insensitive(line, "info")) {
        return LEVEL_INFO;
    }
    return LEVEL_OTHER;
}

static const char *level_color(LogLevel level)
{
    switch (level) {
    case LEVEL_ERROR: return "\033[31;1m";
    case LEVEL_WARNING: return "\033[33;1m";
    case LEVEL_INFO: return "\033[32m";
    case LEVEL_DEBUG: return "\033[36m";
    default: return "";
    }
}

static int line_matches(const Options *options, const char *line, LogLevel *level)
{
    *level = classify_line(line);
    if (options->level != LEVEL_ALL && options->level != *level) {
        return 0;
    }
    return options->search == NULL || contains_case_insensitive(line, options->search);
}

static void process_line(const Options *options, const char *source_path,
                         const char *line, FILE *export_file)
{
    LogLevel level; // NOLINT
    if (!line_matches(options, line, &level)) {
        return;
    }

    if (options->show_source) {
        printf("%s: ", source_path);
    }
    if (options->color) {
        const char *color = level_color(level);
        if (*color != '\0') {
            fputs(color, stdout);
            fputs(line, stdout);
            fputs("\033[0m\n", stdout);
        } else {
            printf("%s\n", line);
        }
    } else {
        printf("%s\n", line);
    }
    fflush(stdout);
    if (export_file != NULL &&
        fprintf(export_file, "%s%s%s\n", options->show_source ? source_path : "",
                options->show_source ? ": " : "", line) < 0) {
        fprintf(stderr, "loglens: failed to write export file\n");
        exit(EXIT_FAILURE);
    }
}

static int append_char(char **line, size_t *length, size_t *capacity, char value)
{
    if (*length + 1 >= *capacity) {
        size_t new_capacity = *capacity == 0 ? 4096 : *capacity * 2;
        char *resized = (char *)realloc(*line, new_capacity);
        if (resized == NULL) {
            fprintf(stderr, "loglens: out of memory while reading a long line\n");
            return 0;
        }
        *line = resized;
        *capacity = new_capacity;
    }
    (*line)[(*length)++] = value;
    return 1;
}

static void finish_line(const Options *options, const char *source_path,
                        char *line, size_t *length,
                        FILE *export_file)
{
    while (*length > 0 && line[*length - 1] == '\r') {
        --*length;
    }
    line[*length] = '\0';
    process_line(options, source_path, line, export_file);
    *length = 0;
}

static void wait_for_more_data(void)
{
#ifdef _WIN32
    Sleep(250);
#else
    struct timespec delay = {0, 250000000L};
    nanosleep(&delay, NULL);
#endif
}

static int same_file(const char *first, const char *second)
{
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION first_info;
    BY_HANDLE_FILE_INFORMATION second_info;
    HANDLE first_handle = CreateFileA(first, 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    HANDLE second_handle = CreateFileA(second, 0,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (first_handle == INVALID_HANDLE_VALUE || second_handle == INVALID_HANDLE_VALUE) {
        if (first_handle != INVALID_HANDLE_VALUE) CloseHandle(first_handle);
        if (second_handle != INVALID_HANDLE_VALUE) CloseHandle(second_handle);
        return 0;
    }
    int matches = GetFileInformationByHandle(first_handle, &first_info) &&
                  GetFileInformationByHandle(second_handle, &second_info) &&
                  first_info.dwVolumeSerialNumber == second_info.dwVolumeSerialNumber &&
                  first_info.nFileIndexHigh == second_info.nFileIndexHigh &&
                  first_info.nFileIndexLow == second_info.nFileIndexLow;
    CloseHandle(first_handle);
    CloseHandle(second_handle);
    return matches;
#else
    struct stat first_stat;
    struct stat second_stat;
    return stat(first, &first_stat) == 0 && stat(second, &second_stat) == 0 &&
           first_stat.st_dev == second_stat.st_dev && first_stat.st_ino == second_stat.st_ino;
#endif
}

static int is_directory_path(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat file_status;
    return stat(path, &file_status) == 0 && S_ISDIR(file_status.st_mode);
#endif
}

static char *join_path(const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    int needs_separator = directory_length > 0 &&
                          directory[directory_length - 1] != '/'
#ifdef _WIN32
                          && directory[directory_length - 1] != '\\'
#endif
                          ;
    char *joined = (char *)malloc(directory_length + name_length +
                                  (size_t)needs_separator + 1);
    if (joined == NULL) {
        fprintf(stderr, "loglens: out of memory while building a path\n");
        return NULL;
    }
    memcpy(joined, directory, directory_length);
    if (needs_separator) {
#ifdef _WIN32
        joined[directory_length++] = '\\';
#else
        joined[directory_length++] = '/';
#endif
    }
    memcpy(joined + directory_length, name, name_length + 1);
    return joined;
}

static int process_file(const Options *options, const char *path, FILE *export_file)
{
    FILE *input = open_input(path);
    char buffer[65536];
    char *line = NULL;
    size_t line_length = 0;
    size_t line_capacity = 0;
    int result = EXIT_SUCCESS;

    if (input == NULL) {
        fprintf(stderr, "loglens: cannot open '%s': %s\n", path, strerror(errno));
        return 0;
    }
    for (;;) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), input);
        for (size_t i = 0; i < bytes_read; ++i) {
            if (buffer[i] == '\n') {
                if (line == NULL && !append_char(&line, &line_length, &line_capacity, '\0')) {
                    result = EXIT_FAILURE;
                    goto done;
                }
                finish_line(options, path, line, &line_length, export_file);
            } else if (!append_char(&line, &line_length, &line_capacity, buffer[i])) {
                result = EXIT_FAILURE;
                goto done;
            }
        }
        if (ferror(input)) {
            fprintf(stderr, "loglens: error reading '%s'\n", path);
            result = EXIT_FAILURE;
            break;
        }
        if (feof(input)) {
            file_offset_t current_position = file_tell(input);
            if (!options->follow) {
                if (line_length > 0) {
                    if (!append_char(&line, &line_length, &line_capacity, '\0')) {
                        result = EXIT_FAILURE;
                        break;
                    }
                    finish_line(options, path, line, &line_length, export_file);
                }
                break;
            }
            clearerr(input);
            wait_for_more_data();
            if (current_position >= 0 && file_seek(input, 0, SEEK_END) == 0) {
                file_offset_t end_position = file_tell(input);
                if (end_position >= 0 && end_position < current_position) {
                    if (file_seek(input, 0, SEEK_SET) != 0) {
                        fprintf(stderr, "loglens: cannot seek in '%s'\n", path);
                        result = EXIT_FAILURE;
                        break;
                    }
                    line_length = 0;
                } else if (file_seek(input, current_position, SEEK_SET) != 0) {
                    fprintf(stderr, "loglens: cannot seek in '%s'\n", path);
                    result = EXIT_FAILURE;
                    break;
                }
            }
        }
    }

done:
    if (fclose(input) != 0) {
        fprintf(stderr, "loglens: failed to close input file '%s'\n", path);
        result = EXIT_FAILURE;
    }
    free(line);
    return result == EXIT_SUCCESS;
}

static int process_path(const Options *options, const char *path, FILE *export_file)
{
    stat_t file_status; // NOLINT
    if (stat(path, &file_status) != 0) {
        fprintf(stderr, "loglens: cannot access '%s': %s\n", path, strerror(errno));
        return 0;
    }
    if (is_directory_path(path)) {
#ifdef _WIN32
        char *pattern = join_path(path, "*");
        WIN32_FIND_DATAA entry; // NOLINT
        if (pattern == NULL) {
            return 0;
        }
        HANDLE search = FindFirstFileA(pattern, &entry);
        free(pattern);
        if (search == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "loglens: cannot list folder '%s' (error %lu)\n",
                    path, (unsigned long)GetLastError());
            return 0;
        }
        int result = 1;
        do {
            if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0 ||
                (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                continue;
            }
            char *child = join_path(path, entry.cFileName);
            if (child == NULL) {
                result = 0;
                continue;
            }
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!process_path(options, child, export_file)) {
                    result = 0;
                }
            } else if (options->export_path == NULL ||
                       !same_file(child, options->export_path)) {
                if (!process_file(options, child, export_file)) {
                    result = 0;
                }
            }
            free(child);
        } while (FindNextFileA(search, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) {
            fprintf(stderr, "loglens: error while listing folder '%s'\n", path);
            result = 0;
        }
        FindClose(search);
        return result;
#else
        DIR *directory = opendir(path);
        int result = 1;
        if (directory == NULL) {
            fprintf(stderr, "loglens: cannot list folder '%s': %s\n", path, strerror(errno));
            return 0;
        }
        struct dirent *entry = readdir(directory);
        while (entry != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                entry = readdir(directory);
                continue;
            }
            char *child = join_path(path, entry->d_name);
            if (child == NULL) {
                result = 0;
                entry = readdir(directory);
                continue;
            }
            struct stat child_status; // NOLINT
            if (lstat(child, &child_status) != 0) {
                fprintf(stderr, "loglens: cannot access '%s': %s\n", child, strerror(errno));
                result = 0;
            } else if (S_ISDIR(child_status.st_mode)) {
                if (!process_path(options, child, export_file)) {
                    result = 0;
                }
            } else if (S_ISREG(child_status.st_mode) &&
                       (options->export_path == NULL ||
                        !same_file(child, options->export_path))) {
                if (!process_file(options, child, export_file)) {
                    result = 0;
                }
            }
            free(child);
            entry = readdir(directory);
        }
        if (closedir(directory) != 0) {
            fprintf(stderr, "loglens: failed to close folder '%s'\n", path);
            result = 0;
        }
        return result;
#endif
    }
    if (options->export_path != NULL && same_file(path, options->export_path)) {
        fprintf(stderr, "loglens: export file must not be the input log file\n");
        return 0;
    }
    return process_file(options, path, export_file);
}

int main(int argc, char **argv)
{
    Options options;
    FILE *export_file = NULL;
    char selected_path[PATH_BUFFER_SIZE];
    stat_t input_status; // NOLINT
    int parse_result = parse_options(argc, argv, &options);
    int result = EXIT_SUCCESS;

    if (parse_result == 2) {
        return EXIT_SUCCESS;
    }
    if (parse_result == 0) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }
    if (options.path == NULL) {
        if (!prompt_for_path(selected_path, sizeof(selected_path))) {
            return EXIT_FAILURE;
        }
        options.path = selected_path;
    }
    if (stat(options.path, &input_status) != 0) {
        fprintf(stderr, "loglens: cannot access '%s': %s\n",
                options.path, strerror(errno));
        return EXIT_FAILURE;
    }
    if (options.follow && is_directory_path(options.path)) {
        fprintf(stderr, "loglens: --follow requires a single log file, not a folder\n");
        return EXIT_FAILURE;
    }
    if (options.export_path != NULL) {
        if (!is_directory_path(options.path) && same_file(options.path, options.export_path)) {
            fprintf(stderr, "loglens: export file must not be the input log file\n");
            return EXIT_FAILURE;
        }
        export_file = fopen(options.export_path, "w");
        if (export_file == NULL) {
            fprintf(stderr, "loglens: cannot open export file '%s': %s\n",
                    options.export_path, strerror(errno));
            return EXIT_FAILURE;
        }
    }
    options.show_source = is_directory_path(options.path);
#ifdef _WIN32
    options.color = !options.no_color && isatty(_fileno(stdout)) &&
                    getenv("NO_COLOR") == NULL;
#else
    options.color = !options.no_color && isatty(fileno(stdout)) &&
                    getenv("NO_COLOR") == NULL;
#endif
    result = process_path(&options, options.path, export_file)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    if (export_file != NULL && fclose(export_file) != 0) {
        fprintf(stderr, "loglens: failed to close export file '%s'\n", options.export_path);
        result = EXIT_FAILURE;
    }
    return result;
}