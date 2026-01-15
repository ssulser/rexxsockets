# rexxsockets

**rexxsockets** is a POSIX-oriented socket extension for **Regina Rexx** on  
**macOS and Linux**.

It provides TCP, UDP, `poll()`-based multiplexing and a modern resolver
(`getaddrinfo`) using Rexx-friendly stem interfaces.

---

## Why rexxsockets?

- **RxSock is more than 20 years old**, designed for obsolete toolchains.
- In practice, RxSock is difficult to maintain and **does not build on modern
  macOS systems (Apple Silicon)**.
- **rexxsockets** is a clean reimplementation based on modern POSIX APIs:
  - `getaddrinfo` / `getnameinfo`
  - `poll`
  - `fcntl(O_NONBLOCK)`
- The API is minimal, explicit, and close to the operating system, making
  debugging and porting straightforward.

---

## Build

### Requirements

- Regina Rexx (including `rexxsaa.h` and `regina-config`)
  - macOS (Homebrew): `brew install regina-rexx`
  - Linux: `regina-rexx` + `regina-rexx-dev`
- C compiler (`clang` or `gcc`)
- POSIX system with sockets and `poll()`

### Build command

```sh
make
````

### Result

* macOS: `librexxsockets.dylib`
* Linux: `librexxsockets.so`

---

## Loading the library (classic Regina extension pattern)

Like other Regina extensions (e.g. `SQLLoadFuncs` in rexxsql),
**rexxsockets exposes a single loader function**.

### RSLoadFuncs()

Registers all Rexx-callable functions in C using
`RexxRegisterFunctionExe()`.

```rexx
call RxFuncAdd "RSLoadFuncs", "rexxsockets", "RSLoadFuncs"
rc = RSLoadFuncs()
if rc \= 0 then do
  say "RSLoadFuncs failed, rc="rc
  exit 1
end
```

After this, all functions are directly available.

```rexx
say RSVersion()
```

### RSUnloadFuncs()

Optional cleanup helper that deregisters the functions again.
Useful for interactive sessions or tests.

```rexx
call RSUnloadFuncs
```

---

## Mini API Index

* General: `RSVersion`, `RSLastError`, `RSUnloadFuncs`
* TCP: `RSTCPConnect`, `RSTCPListen`, `RSAccept`
* I/O: `RSSend`, `RSRecv`, `RSClose`
* Multiplexing: `RSPoll`
* Blocking control: `RSSetBlocking`
* UDP: `RSUDPBind`, `RSUDPSend`, `RSUDPRecv`
* Resolver: `RSResolve`

---

# API Reference

## General

### RSVersion()

Returns a version string.

```rexx
v = RSVersion()
```

Returns a string, e.g. `"rexxsockets 0.x"`.

---

### RSLastError()

Returns the last error message set by a rexxsockets function.

```rexx
e = RSLastError()
```

Returns a string (may be empty).

---

## TCP

### RSTCPListen(bindHost, service [, backlog])

Create a TCP listening socket.

```rexx
listenFd = RSTCPListen(bindHost, service, backlog)
```

* `bindHost` – local address (empty = all interfaces)
* `service` – port or service name
* `backlog` – optional, default `64`

Returns a socket FD or `-1` on error.

---

### RSAccept(listenFd [, timeout_ms] [, stemName])

Accept an incoming connection.

```rexx
peer. = ""
fd = RSAccept(listenFd, 5000, "peer.")
```

Output stem:

* `peer.host`
* `peer.port`

Returns a new socket FD or `-1`.

---

### RSTCPConnect(host, service [, timeout_ms])

Connect to a TCP server.

```rexx
fd = RSTCPConnect("example.com", "80", 3000)
```

Returns a socket FD or `-1`.

---

## I/O

### RSSend(fd, data [, flags])

Send bytes on a socket.

```rexx
n = RSSend(fd, data)
```

* Binary-safe
* Returns bytes sent or `-1`

**SIGPIPE handling**

* macOS/BSD: `SO_NOSIGPIPE`
* Linux: `MSG_NOSIGNAL`
* No global signal handlers are modified

---

### RSRecv(fd, maxBytes [, timeout_ms])

Receive bytes from a socket.

```rexx
data = RSRecv(fd, 4096, 3000)
```

Returns received data or `""` on error/timeout.

---

### RSClose(fd)

Close a socket.

```rexx
call RSClose fd
```

---

## Socket mode and multiplexing

### RSSetBlocking(fd, blocking)

Enable or disable blocking mode.

```rexx
call RSSetBlocking fd, 0   /* non-blocking */
```

---

### RSPoll(inStem, timeout_ms, outStem)

Expose POSIX `poll()`.

```rexx
in.0 = 1
in.1.fd = listenFd
in.1.events = "IN"

rc = RSPoll("in.", 1000, "out.")
```

Returns number of ready FDs, `0` on timeout, or `-1`.

---

## UDP

### RSUDPBind(bindHost, service)

Bind a UDP socket.

```rexx
udp = RSUDPBind("127.0.0.1", "9999")
```

---

### RSUDPSend(fd, host, service, data)

Send a UDP datagram.

---

### RSUDPRecv(fd, maxBytes [, timeout_ms] [, stemName])

Receive a UDP datagram.

```rexx
peer. = ""
data = RSUDPRecv(udp, 2048, 5000, "peer.")
```

---

## Resolver

### RSResolve(host, service, stemName [, family [, socktype [, protocol]]])

Wrapper for `getaddrinfo()`.

```rexx
res. = ""
rc = RSResolve("example.com", "80", "res.")
```

Optional filters:

* `family` (0 = AF_UNSPEC, 2 = AF_INET, 30 = AF_INET6)
* `socktype` (1 = STREAM, 2 = DGRAM)
* `protocol` (6 = TCP, 17 = UDP)

---

## Tests and examples

* `examples/tcp_client.rexx` – external HTTP test
* `examples/tcp_client_local.rexx` – loopback client
* `examples/sigpipe_test.rexx` – SIGPIPE safety check
* `examples/echo_server.rexx` – poll-based TCP server
* `examples/udp_echo.rexx` – UDP echo example

---

## Portability notes

### NI_MAXHOST / NI_MAXSERV

Some platforms (notably macOS) do not reliably define these macros.
rexxsockets uses internal fallbacks.

---

## License

LGPL-2.1-or-later (compatible with the Regina Rexx license base).