# http-server

![http-server banner](https://github.com/user-attachments/assets/3188745d-bf68-4b4b-a25c-724ad38ab4d4)

## Description

http-server is a custom HTTP/1.1 web server written in C++98, built from scratch.
The server handles GET, POST, and DELETE requests, serves static files,
supports file uploads via multipart/form-data, executes CGI scripts (Python), and is
configured via NGINX-inspired configuration files.

Key features:

- Non-blocking I/O using kqueue (macOS) / epoll (Linux) — a single event loop handles all sockets and pipes
- NGINX-style configuration with multiple `server {}` blocks on different ports
- Static file serving with automatic MIME type detection
- Directory listing (`autoindex`)
- File upload via multipart/form-data with configurable storage location
- CGI execution (Python) with per-request timeout and zombie-process prevention
- Custom error pages per status code
- HTTP/1.1 keep-alive connection support
- Accurate HTTP status codes and default error pages when none are configured

## Instructions

### Compilation

```bash
make        # Build the http-server executable
make clean  # Remove object files
make fclean # Remove all generated files including executable
make re     # Clean rebuild (fclean + all)
```

Compiles with `c++ -Wall -Wextra -Werror -std=c++98` on both macOS and Linux.

### Running

```bash
./http-server                         # Uses default config (configs/default.conf)
./http-server configs/default.conf    # Static file serving + uploads on port 8080
./http-server configs/cgi.conf        # Adds /cgi-bin route with Python CGI
./http-server configs/multi.conf      # Two servers on ports 8080 and 8081
```

### Configuration

Configuration files follow an NGINX-inspired syntax. Example:

```nginx
server {
    listen 8080;
    server_name localhost;

    error_page 404 ./www/errors/404.html;
    client_max_body_size 1M;

    location / {
        methods GET;
        root ./www;
        index index.html;
        autoindex off;
    }

    location /uploads {
        methods GET POST DELETE;
        root ./www/uploads;
        upload_dir ./www/uploads;
        autoindex on;
    }

    location /cgi-bin {
        methods GET POST;
        root ./www/cgi-bin;
        cgi_extension .py;
        cgi_path /usr/bin/python3;
    }
}
```

Supported directives: `listen`, `server_name`, `error_page`, `client_max_body_size`,
`methods`, `root`, `index`, `autoindex`, `upload_dir`, `cgi_extension`, `cgi_path`,
`return` (redirect).

### Testing

`test_all.py` is the master runner. It compiles and runs the C++ unit tests first
(no server needed), then waits for the server and runs all HTTP suites.

```bash
# Start server (use cgi.conf to enable all suites including CGI)
./http-server configs/cgi.conf &

# Run everything
python3 tests/test_all.py 8080

# Skip individual suites
python3 tests/test_all.py 8080 --no-stress
python3 tests/test_all.py 8080 --no-cgi --no-stress
```

Individual suites:

| File | What it tests | Needs server |
|---|---|---|
| `tests/test_config.cpp` | Config parser: ports, directives, size suffixes, error cases | No |
| `tests/test_socket.cpp` | Socket class: bind, non-blocking, SO_REUSEADDR, close | No |
| `tests/test_basic.py` | GET, required headers, keep-alive, pipelining | Yes |
| `tests/test_methods.py` | GET, POST upload, DELETE, 405 for unsupported methods | Yes |
| `tests/test_errors.py` | 400, 404, 405, 413, 414, custom error pages | Yes |
| `tests/test_upload.py` | multipart/form-data upload, retrieve, delete, size limit | Yes |
| `tests/test_cgi.py` | CGI execution, env vars, POST body, timeout → 504 | Yes (cgi.conf) |
| `tests/test_edge_cases.py` | Partial requests, binary garbage, long URIs, simultaneous connections | Yes |
| `tests/test_stress.py` | 50 concurrent requests, 10 s sustained load | Yes |

```bash
# Conformance tester binary (if available)
./tester http://localhost:8080
```

### Memory leak check

**macOS:**
```bash
./http-server configs/default.conf &
python3 tests/test_all.py 8080
leaks $!
kill $!
```

**Linux:**
```bash
valgrind --leak-check=full --show-leak-kinds=all ./http-server configs/default.conf &
python3 tests/test_all.py 8080
# Ctrl+C the server and review valgrind output
```

## Resources

- [RFC 2616 — HTTP/1.1](https://www.rfc-editor.org/rfc/rfc2616) — HTTP protocol specification
- [RFC 3875 — CGI/1.1](https://www.rfc-editor.org/rfc/rfc3875) — CGI interface specification
- [NGINX documentation](https://nginx.org/en/docs/) — Configuration format reference
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) — Socket programming reference
- [kqueue(2) man page](https://man.freebsd.org/cgi/man.cgi?query=kqueue) — macOS I/O event notification
- [epoll(7) man page](https://man7.org/linux/man-pages/man7/epoll.7.html) — Linux I/O event notification

## License

Released under the [MIT License](LICENSE).
