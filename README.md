# LogLens

A lightweight, cross-platform command-line log viewer with filtering, coloring,
search, follow mode, and export capabilities.

LogLens reads single log files or walks entire folders recursively, classifies
each line by severity (error, warning, info, debug, other), and displays matches
with optional ANSI color highlighting. It runs on Windows, Linux, and macOS.

---

## Features

- File or folder input: analyze a single log file or scan a directory recursively
- Case-insensitive search with `-s`
- Severity filtering with `-l error|warning|info|debug|other|all`
- ANSI color output per severity, automatically disabled when piped
- Follow mode with `-f` to print new lines as they arrive
- Export matching lines to a file with `-e`
- Multi-file mode: when scanning a folder, the source file is prepended to each line
- Native Windows file picker when no path is given

---

## Building

LogLens is a single C file with no external dependencies.

### Linux / macOS

    gcc -O2 -Wall -Wextra -o loglens main.c

### Windows (MSVC)

    cl /O2 /W4 main.c /Fe:loglens.exe

### Windows (MinGW)

    gcc -O2 -Wall -Wextra -o loglens.exe main.c -lcomdlg32

---

## Usage

    loglens [options] [<logfile-or-folder>]

If no path is given, Windows opens the file picker; on other terminals you will
be prompted for a path.

### Options

| Option | Description |
| --- | --- |
| `-s, --search TEXT` | Show only lines containing TEXT (case-insensitive) |
| `-l, --level LEVEL` | Filter by error, warning, info, debug, other, or all |
| `-f, --follow` | Continue showing lines as the file grows |
| `-e, --export FILE` | Write matching lines to FILE |
| `--no-color` | Disable colored output |
| `-h, --help` | Show help |

### Environment variables

| Variable | Effect |
| --- | --- |
| `NO_COLOR` | If set, disables color output |

---

## Examples

Show only errors from a log file:

    loglens -l error app.log

Follow a log file and highlight warnings in real time:

    loglens -f -l warning server.log

Search for a request ID across a folder and export matches:

    loglens -s "req=abc123" -e matches.log /var/log/myapp

Scan every log file in a directory, show only debug lines, no colors:

    loglens --no-color -l debug ./logs

Pipe into another tool (colors are automatically disabled):

    loglens -l error app.log | grep "database"

---

## How lines are classified

LogLens uses simple case-insensitive keyword matching:

| Level | Matches (case-insensitive) |
| --- | --- |
| Error | fatal, error, err:, err followed by space |
| Warning | warning, warn:, warn followed by space |
| Debug | debug, trace |
| Info | info |
| Other | anything that does not match above |

Classification happens in the order shown; the first match wins. A line
containing both "error" and "warning" is classified as Error.

---

## Behavior notes

- Follow mode uses polling (250 ms) and detects log rotation by comparing the
  file position before and after the poll. If the file shrinks, LogLens reopens
  from the beginning.
- Export file safety: if the export path is the same file as an input log, or
  lives inside the scanned folder and equals an input, LogLens refuses to run to
  avoid destroying your logs.
- Symlinks: on POSIX systems symlinked directories are skipped to prevent
  infinite recursion; on Windows, reparse points are skipped for the same
  reason.
- Windows input: the file handle is opened with FILE_SHARE_READ, FILE_SHARE_WRITE
  and FILE_SHARE_DELETE, so log rotation and tailing still work when the writer
  holds a lock on the file.

---

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Error (bad arguments, unreadable file, export failure, etc.) |

---

## License

This project is licensed under the MIT License. See the LICENSE file for
details.

---

## Contributing

Pull requests are welcome. For major changes, please open an issue first to
discuss what you would like to change.

When submitting changes, please:

1. Keep the code warning-free with -Wall -Wextra (GCC/Clang) or /W4 (MSVC).
2. Test on at least one POSIX system and on Windows if possible.
3. Keep the codebase as a single translation unit with no external dependencies.