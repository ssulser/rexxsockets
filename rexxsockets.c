#define INCL_RXFUNC
#define INCL_RXSHV

#include "rexxsaa.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

#ifndef RS_ERRBUF_SZ
#define RS_ERRBUF_SZ 256
#endif

static char g_last_error[RS_ERRBUF_SZ];

/* Disable SIGPIPE per-socket where supported (macOS/BSD).
   Linux fallback is MSG_NOSIGNAL in RSSend() when available. */
static void rs_disable_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
  int one = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, (socklen_t)sizeof(one));
#else
  (void)fd;
#endif
}


/* ---------- error helpers ---------- */

static void rs_set_error(const char *prefix) {
  int e = errno;
  const char *msg = strerror(e);
  if (!prefix) prefix = "ERR";
  snprintf(g_last_error, sizeof(g_last_error), "%s: (%d) %s", prefix, e, msg ? msg : "unknown");
}

static void rs_set_error_text(const char *txt) {
  if (!txt) txt = "";
  snprintf(g_last_error, sizeof(g_last_error), "%s", txt);
}

static void rs_clear_error(void) {
  g_last_error[0] = '\0';
}

/* ---------- parsing / result helpers ---------- */

static int rs_parse_int(const RXSTRING *s, int *out) {
  if (!s || !s->strptr) return 0;
  char *end = NULL;
  errno = 0;
  long v = strtol(s->strptr, &end, 10);
  if (errno != 0) return 0;
  if (end == s->strptr) return 0;
  *out = (int)v;
  return 1;
}

static APIRET rs_set_result_text(PRXSTRING result, const char *txt) {
  if (!txt) txt = "";
  size_t n = strlen(txt);
  result->strptr = (char*)RexxAllocateMemory(n);
  if (!result->strptr) return 40;
  memcpy(result->strptr, txt, n);
  result->strlength = (ULONG)n;
  return 0;
}

static APIRET rs_set_result_int(PRXSTRING result, long v) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%ld", v);
  return rs_set_result_text(result, buf);
}

static APIRET rs_set_result_bin(PRXSTRING result, const void *data, size_t n) {
  if (!data || n == 0) {
    result->strptr = (char*)RexxAllocateMemory(0);
    result->strlength = 0;
    return 0;
  }
  result->strptr = (char*)RexxAllocateMemory(n);
  if (!result->strptr) return 40;
  memcpy(result->strptr, data, n);
  result->strlength = (ULONG)n;
  return 0;
}

/* ---------- Rexx variable pool helpers (stems) ---------- */

static int rs_stem_normalize(const char *stem_in, char *stem_out, size_t outsz) {
  if (!stem_in || !*stem_in) return 0;
  size_t n = strlen(stem_in);
  if (n + 2 > outsz) return 0;
  strcpy(stem_out, stem_in);
  if (stem_out[n-1] != '.') {
    stem_out[n] = '.';
    stem_out[n+1] = '\0';
  }
  return 1;
}

static int rs_set_var(const char *name, const char *value) {
  SHVBLOCK shv;
  memset(&shv, 0, sizeof(shv));

  RXSTRING vn, vv;
  vn.strptr = (char*)name;
  vn.strlength = (ULONG)strlen(name);

  vv.strptr = (char*)(value ? value : "");
  vv.strlength = (ULONG)strlen(value ? value : "");

  shv.shvnext = NULL;
  shv.shvname = vn;
  shv.shvvalue = vv;
  shv.shvcode = RXSHV_SYSET;
  shv.shvret = 0;

  APIRET rc = RexxVariablePool(&shv);
  return (rc == RXSHV_OK || rc == RXSHV_NEWV) ? 1 : 0;
}

static int rs_get_var(const char *name, char *buf, size_t bufsz) {
  /* Simple fetch with fixed buffer. Enough for our stems (fds/events). */
  SHVBLOCK shv;
  memset(&shv, 0, sizeof(shv));

  RXSTRING vn, vv;
  vn.strptr = (char*)name;
  vn.strlength = (ULONG)strlen(name);

  vv.strptr = buf;
  vv.strlength = (ULONG)(bufsz ? bufsz - 1 : 0);

  shv.shvnext = NULL;
  shv.shvname = vn;
  shv.shvvalue = vv;
  shv.shvcode = RXSHV_SYFET;
  shv.shvret = 0;

  APIRET rc = RexxVariablePool(&shv);
  if (rc != RXSHV_OK) return 0;

  /* Ensure NUL termination */
  size_t n = (size_t)shv.shvvalue.strlength;
  if (n >= bufsz) n = bufsz - 1;
  buf[n] = '\0';
  return 1;
}

static void rs_set_stem_kv(const char *stem, const char *key, const char *val) {
  char name[256];
  snprintf(name, sizeof(name), "%s%s", stem, key);
  (void)rs_set_var(name, val);
}

static void rs_set_stem_kv_int(const char *stem, const char *key, long v) {
  char val[64];
  snprintf(val, sizeof(val), "%ld", v);
  rs_set_stem_kv(stem, key, val);
}

/* ---------- socket helpers ---------- */

static int rs_connect_with_timeout(int fd, const struct sockaddr *sa, socklen_t slen, int timeout_ms) {
  if (timeout_ms <= 0) {
    return connect(fd, sa, slen);
  }

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return -1;

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;

  int rc = connect(fd, sa, slen);
  if (rc == 0) {
    (void)fcntl(fd, F_SETFL, flags);
    return 0;
  }

  if (errno != EINPROGRESS) {
    (void)fcntl(fd, F_SETFL, flags);
    return -1;
  }

  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLOUT;
  pfd.revents = 0;

  rc = poll(&pfd, 1, timeout_ms);
  if (rc <= 0) {
    if (rc == 0) errno = ETIMEDOUT;
    (void)fcntl(fd, F_SETFL, flags);
    return -1;
  }

  int soerr = 0;
  socklen_t len = sizeof(soerr);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0) {
    (void)fcntl(fd, F_SETFL, flags);
    return -1;
  }
  if (soerr != 0) {
    errno = soerr;
    (void)fcntl(fd, F_SETFL, flags);
    return -1;
  }

  (void)fcntl(fd, F_SETFL, flags);
  return 0;
}

/* ---------- exported Rexx functions ---------- */

APIRET RSVersion(const char *name, LONG argc, RXSTRING argv[],
                  const char *queue, PRXSTRING result) {
  (void)name; (void)argc; (void)argv; (void)queue;
  rs_clear_error();
  return rs_set_result_text(result, "rexxsockets 0.2");
}

APIRET RSLastError(const char *name, LONG argc, RXSTRING argv[],
                    const char *queue, PRXSTRING result) {
  (void)name; (void)argc; (void)argv; (void)queue;
  return rs_set_result_text(result, g_last_error);
}

/* RSTCPConnect(host, service[, timeout_ms]) -> fd or -1 */
APIRET RSTCPConnect(const char *name, LONG argc, RXSTRING argv[],
                     const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 2) return rs_set_result_int(result, -1);

  const char *host = argv[0].strptr;
  const char *serv = argv[1].strptr;

  int timeout_ms = 0;
  if (argc >= 3) (void)rs_parse_int(&argv[2], &timeout_ms);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo *res = NULL;
  int gai = getaddrinfo(host, serv, &hints, &res);
  if (gai != 0) {
    char buf[RS_ERRBUF_SZ];
    snprintf(buf, sizeof(buf), "getaddrinfo: %s", gai_strerror(gai));
    rs_set_error_text(buf);
    return rs_set_result_int(result, -1);
  }

  int fd = -1;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) continue;

    if (rs_connect_with_timeout(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen, timeout_ms) == 0) {
      freeaddrinfo(res);
      return rs_set_result_int(result, fd);
    }

    close(fd);
    fd = -1;
  }

  rs_set_error("connect");
  freeaddrinfo(res);
  return rs_set_result_int(result, -1);
}

/* RSTCPListen(bindHost, service[, backlog]) -> listen_fd or -1
   bindHost: "" or "0.0.0.0" or "::" or "127.0.0.1" etc.
*/
APIRET RSTCPListen(const char *name, LONG argc, RXSTRING argv[],
                    const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 2) return rs_set_result_int(result, -1);

  const char *bindHost = argv[0].strptr;
  const char *serv     = argv[1].strptr;

  int backlog = 64;
  if (argc >= 3) (void)rs_parse_int(&argv[2], &backlog);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *res = NULL;
  int gai = getaddrinfo((bindHost && *bindHost) ? bindHost : NULL, serv, &hints, &res);
  if (gai != 0) {
    char buf[RS_ERRBUF_SZ];
    snprintf(buf, sizeof(buf), "getaddrinfo: %s", gai_strerror(gai));
    rs_set_error_text(buf);
    return rs_set_result_int(result, -1);
  }

  int fd = -1;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) continue;

    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));

    if (bind(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0) {
      if (listen(fd, backlog) == 0) {
        freeaddrinfo(res);
        return rs_set_result_int(result, fd);
      }
      rs_set_error("listen");
      close(fd);
      fd = -1;
      break;
    }

    close(fd);
    fd = -1;
  }

  if (fd < 0 && g_last_error[0] == '\0') rs_set_error("bind");
  freeaddrinfo(res);
  return rs_set_result_int(result, -1);
}

/* RSAccept(listenFd[, timeout_ms, stem]) -> newFd or -1
   stem (optional) gets:
     stem.host, stem.port
*/
APIRET RSAccept(const char *name, LONG argc, RXSTRING argv[],
                 const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 1) return rs_set_result_int(result, -1);

  int listenFd = -1;
  if (!rs_parse_int(&argv[0], &listenFd)) {
    rs_set_error_text("bad listen fd");
    return rs_set_result_int(result, -1);
  }

  int timeout_ms = 0;
  if (argc >= 2) (void)rs_parse_int(&argv[1], &timeout_ms);

  char stem[128] = {0};
  int wantStem = 0;
  if (argc >= 3 && argv[2].strptr && argv[2].strlength > 0) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%.*s", (int)argv[2].strlength, argv[2].strptr);
    wantStem = rs_stem_normalize(tmp, stem, sizeof(stem));
  }

  if (timeout_ms > 0) {
    struct pollfd pfd;
    pfd.fd = listenFd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) {
      if (rc == 0) errno = ETIMEDOUT;
      rs_set_error(rc == 0 ? "accept timeout" : "poll");
      return rs_set_result_int(result, -1);
    }
  }

  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  int newFd = accept(listenFd, (struct sockaddr*)&ss, &slen);
  if (newFd < 0) {
    rs_set_error("accept");
    return rs_set_result_int(result, -1);
  }

  rs_disable_sigpipe(newFd);
  if (wantStem) {
    char host[NI_MAXHOST], serv[NI_MAXSERV];
    int rc = getnameinfo((struct sockaddr*)&ss, slen,
                         host, sizeof(host), serv, sizeof(serv),
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc == 0) {
      rs_set_stem_kv(stem, "host", host);
      rs_set_stem_kv(stem, "port", serv);
    }
  }

  return rs_set_result_int(result, newFd);
}

/* RSSetBlocking(fd, 0|1) -> 0 or -1 */
APIRET RSSetBlocking(const char *name, LONG argc, RXSTRING argv[],
                      const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 2) return rs_set_result_int(result, -1);

  int fd = -1, on = 1;
  if (!rs_parse_int(&argv[0], &fd) || !rs_parse_int(&argv[1], &on)) {
    rs_set_error_text("bad arguments");
    return rs_set_result_int(result, -1);
  }

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    rs_set_error("fcntl");
    return rs_set_result_int(result, -1);
  }

  if (on) flags &= ~O_NONBLOCK;
  else   flags |=  O_NONBLOCK;

  if (fcntl(fd, F_SETFL, flags) < 0) {
    rs_set_error("fcntl");
    return rs_set_result_int(result, -1);
  }

  return rs_set_result_int(result, 0);
}

/* RSPoll(inStem, timeout_ms, outStem) -> readyCount or -1
   inStem:
     in.0 = N
     in.1.fd, in.1.events   (events string contains: R W E)
   outStem:
     out.0 = readyCount
     out.1.fd, out.1.revents (revents string: R W E H N)
*/
static short rs_events_from_str(const char *s) {
  short ev = 0;
  if (!s) return 0;
  for (; *s; s++) {
    if (*s=='R' || *s=='r') ev |= POLLIN;
    if (*s=='W' || *s=='w') ev |= POLLOUT;
    if (*s=='E' || *s=='e') ev |= POLLERR;
  }
  return ev;
}

static void rs_revents_to_str(short re, char *out, size_t outsz) {
  /* R W E H N */
  char *p = out;
  char *end = out + (outsz ? outsz-1 : 0);
  if (p >= end) { if (outsz) out[0]='\0'; return; }

  if (re & POLLIN)  { if (p<end) *p++='R'; }
  if (re & POLLOUT) { if (p<end) *p++='W'; }
  if (re & POLLERR) { if (p<end) *p++='E'; }
  if (re & POLLHUP) { if (p<end) *p++='H'; }
#ifdef POLLNVAL
  if (re & POLLNVAL){ if (p<end) *p++='N'; }
#endif
  *p = '\0';
}

APIRET RSPoll(const char *name, LONG argc, RXSTRING argv[],
               const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 3) return rs_set_result_int(result, -1);

  char inStem[128], outStem[128];
  snprintf(inStem, sizeof(inStem), "%.*s", (int)argv[0].strlength, argv[0].strptr);
  snprintf(outStem, sizeof(outStem), "%.*s", (int)argv[2].strlength, argv[2].strptr);

  if (!rs_stem_normalize(inStem, inStem, sizeof(inStem)) ||
      !rs_stem_normalize(outStem, outStem, sizeof(outStem))) {
    rs_set_error_text("bad stem name");
    return rs_set_result_int(result, -1);
  }

  int timeout_ms = 0;
  (void)rs_parse_int(&argv[1], &timeout_ms);

  char var[256], buf[256];
  snprintf(var, sizeof(var), "%s0", inStem);
  if (!rs_get_var(var, buf, sizeof(buf))) {
    rs_set_error_text("missing inStem.0");
    return rs_set_result_int(result, -1);
  }

  int n = atoi(buf);
  if (n < 0 || n > 4096) {
    rs_set_error_text("bad inStem.0");
    return rs_set_result_int(result, -1);
  }

  struct pollfd *pfds = (struct pollfd*)calloc((size_t)n, sizeof(struct pollfd));
  if (!pfds) {
    rs_set_error_text("calloc failed");
    return rs_set_result_int(result, -1);
  }

  for (int i=1; i<=n; i++) {
    /* in.i.fd */
    snprintf(var, sizeof(var), "%s%d.fd", inStem, i);
    if (!rs_get_var(var, buf, sizeof(buf))) { pfds[i-1].fd = -1; continue; }
    pfds[i-1].fd = atoi(buf);

    /* in.i.events */
    snprintf(var, sizeof(var), "%s%d.events", inStem, i);
    if (rs_get_var(var, buf, sizeof(buf))) pfds[i-1].events = rs_events_from_str(buf);
    else pfds[i-1].events = POLLIN;
  }

  int rc = poll(pfds, (nfds_t)n, timeout_ms);
  if (rc < 0) {
    rs_set_error("poll");
    free(pfds);
    return rs_set_result_int(result, -1);
  }

  /* build outStem */
  rs_set_stem_kv_int(outStem, "0", rc);

  int outi = 0;
  for (int i=0; i<n; i++) {
    if (pfds[i].revents == 0) continue;
    outi++;

    char key[64];
    /* out.outi.fd */
    snprintf(key, sizeof(key), "%d.fd", outi);
    rs_set_stem_kv_int(outStem, key, pfds[i].fd);

    /* out.outi.revents */
    char rebuf[16];
    rs_revents_to_str(pfds[i].revents, rebuf, sizeof(rebuf));
    snprintf(key, sizeof(key), "%d.revents", outi);
    rs_set_stem_kv(outStem, key, rebuf);
  }

  free(pfds);
  return rs_set_result_int(result, rc);
}

/* UDP: RSUDPBind(bindHost, service) -> fd or -1 */
APIRET RSUDPBind(const char *name, LONG argc, RXSTRING argv[],
                  const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 2) return rs_set_result_int(result, -1);

  const char *bindHost = argv[0].strptr;
  const char *serv     = argv[1].strptr;

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *res = NULL;
  int gai = getaddrinfo((bindHost && *bindHost) ? bindHost : NULL, serv, &hints, &res);
  if (gai != 0) {
    char buf[RS_ERRBUF_SZ];
    snprintf(buf, sizeof(buf), "getaddrinfo: %s", gai_strerror(gai));
    rs_set_error_text(buf);
    return rs_set_result_int(result, -1);
  }

  int fd = -1;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) continue;

    if (bind(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0) {
      freeaddrinfo(res);
      return rs_set_result_int(result, fd);
    }
    close(fd);
    fd = -1;
  }

  rs_set_error("bind");
  freeaddrinfo(res);
  return rs_set_result_int(result, -1);
}

/* RSUDPSend(fd, host, service, data) -> bytes or -1 */
APIRET RSUDPSend(const char *name, LONG argc, RXSTRING argv[],
                  const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 4) return rs_set_result_int(result, -1);

  int fd = -1;
  if (!rs_parse_int(&argv[0], &fd)) {
    rs_set_error_text("bad fd");
    return rs_set_result_int(result, -1);
  }

  const char *host = argv[1].strptr;
  const char *serv = argv[2].strptr;

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  struct addrinfo *res = NULL;
  int gai = getaddrinfo(host, serv, &hints, &res);
  if (gai != 0) {
    char buf[RS_ERRBUF_SZ];
    snprintf(buf, sizeof(buf), "getaddrinfo: %s", gai_strerror(gai));
    rs_set_error_text(buf);
    return rs_set_result_int(result, -1);
  }

  ssize_t n = -1;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    n = sendto(fd, argv[3].strptr, (size_t)argv[3].strlength, 0,
               ai->ai_addr, (socklen_t)ai->ai_addrlen);
    if (n >= 0) break;
  }

  if (n < 0) rs_set_error("sendto");
  freeaddrinfo(res);
  return rs_set_result_int(result, (n < 0) ? -1 : (long)n);
}

/* RSUDPRecv(fd, maxBytes[, timeout_ms, stem]) -> data ("" on timeout/error)
   stem.host, stem.port filled with peer (numeric)
*/
APIRET RSUDPRecv(const char *name, LONG argc, RXSTRING argv[],
                  const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 2) return rs_set_result_text(result, "");

  int fd = -1, maxBytes = 0;
  if (!rs_parse_int(&argv[0], &fd) || !rs_parse_int(&argv[1], &maxBytes) || maxBytes < 0) {
    rs_set_error_text("bad arguments");
    return rs_set_result_text(result, "");
  }

  int timeout_ms = 0;
  if (argc >= 3) (void)rs_parse_int(&argv[2], &timeout_ms);

  char stem[128] = {0};
  int wantStem = 0;
  if (argc >= 4 && argv[3].strptr && argv[3].strlength > 0) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%.*s", (int)argv[3].strlength, argv[3].strptr);
    wantStem = rs_stem_normalize(tmp, stem, sizeof(stem));
  }

  if (timeout_ms > 0) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) {
      if (rc == 0) errno = ETIMEDOUT;
      rs_set_error(rc == 0 ? "udp recv timeout" : "poll");
      return rs_set_result_text(result, "");
    }
  }

  char *buf = (char*)malloc((size_t)maxBytes);
  if (!buf) {
    rs_set_error_text("malloc failed");
    return rs_set_result_text(result, "");
  }

  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  ssize_t n = recvfrom(fd, buf, (size_t)maxBytes, 0, (struct sockaddr*)&ss, &slen);
  if (n < 0) {
    rs_set_error("recvfrom");
    free(buf);
    return rs_set_result_text(result, "");
  }

  if (wantStem) {
    char host[NI_MAXHOST], serv[NI_MAXSERV];
    int rc = getnameinfo((struct sockaddr*)&ss, slen,
                         host, sizeof(host), serv, sizeof(serv),
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc == 0) {
      rs_set_stem_kv(stem, "host", host);
      rs_set_stem_kv(stem, "port", serv);
    }
  }

  APIRET rrc = rs_set_result_bin(result, buf, (size_t)n);
  free(buf);
  return rrc;
}

/* RSResolve(host, service, stem[, family, socktype, protocol]) -> count or -1
   stem.0 = count
   stem.i.host, stem.i.port, stem.i.family, stem.i.socktype, stem.i.protocol
*/
APIRET RSResolve(const char *name, LONG argc, RXSTRING argv[],
                  const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 3) return rs_set_result_int(result, -1);

  const char *host = argv[0].strptr;
  const char *serv = argv[1].strptr;

  char stem[128];
  snprintf(stem, sizeof(stem), "%.*s", (int)argv[2].strlength, argv[2].strptr);
  if (!rs_stem_normalize(stem, stem, sizeof(stem))) {
    rs_set_error_text("bad stem name");
    return rs_set_result_int(result, -1);
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
  hints.ai_protocol = 0;

  int family=0, socktype=0, proto=0;
  if (argc >= 4) (void)rs_parse_int(&argv[3], &family);
  if (argc >= 5) (void)rs_parse_int(&argv[4], &socktype);
  if (argc >= 6) (void)rs_parse_int(&argv[5], &proto);

  if (family)   hints.ai_family = family;
  if (socktype) hints.ai_socktype = socktype;
  if (proto)    hints.ai_protocol = proto;

  struct addrinfo *res = NULL;
  int gai = getaddrinfo(host, serv, &hints, &res);
  if (gai != 0) {
    char buf[RS_ERRBUF_SZ];
    snprintf(buf, sizeof(buf), "getaddrinfo: %s", gai_strerror(gai));
    rs_set_error_text(buf);
    return rs_set_result_int(result, -1);
  }

  int count = 0;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    count++;
    char hostb[NI_MAXHOST], servb[NI_MAXSERV];
    int rc = getnameinfo(ai->ai_addr, (socklen_t)ai->ai_addrlen,
                         hostb, sizeof(hostb), servb, sizeof(servb),
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
      strcpy(hostb, "");
      strcpy(servb, "");
    }

    char key[64];
    snprintf(key, sizeof(key), "%d.host", count);
    rs_set_stem_kv(stem, key, hostb);

    snprintf(key, sizeof(key), "%d.port", count);
    rs_set_stem_kv(stem, key, servb);

    snprintf(key, sizeof(key), "%d.family", count);
    rs_set_stem_kv_int(stem, key, (long)ai->ai_family);

    snprintf(key, sizeof(key), "%d.socktype", count);
    rs_set_stem_kv_int(stem, key, (long)ai->ai_socktype);

    snprintf(key, sizeof(key), "%d.protocol", count);
    rs_set_stem_kv_int(stem, key, (long)ai->ai_protocol);
  }

  rs_set_stem_kv_int(stem, "0", count);
  freeaddrinfo(res);
  return rs_set_result_int(result, count);
}

/* RSSend(fd, data[, flags]) -> bytesSent or -1 */
APIRET RSSend(const char *name, LONG argc, RXSTRING argv[],
               const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 2) return rs_set_result_int(result, -1);

  int fd = -1;
  if (!rs_parse_int(&argv[0], &fd)) {
    rs_set_error_text("bad fd");
    return rs_set_result_int(result, -1);
  }

  int flags = 0;
  if (argc >= 3) (void)rs_parse_int(&argv[2], &flags);


#ifdef MSG_NOSIGNAL
  flags |= MSG_NOSIGNAL;
#endif
  ssize_t n = send(fd, argv[1].strptr, (size_t)argv[1].strlength, flags);
  if (n < 0) {
    rs_set_error("send");
    return rs_set_result_int(result, -1);
  }
  return rs_set_result_int(result, (long)n);
}

/* RSRecv(fd, maxBytes[, timeout_ms]) -> data ("" on timeout/error) */
APIRET RSRecv(const char *name, LONG argc, RXSTRING argv[],
               const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 2) return rs_set_result_text(result, "");

  int fd = -1, maxBytes = 0;
  if (!rs_parse_int(&argv[0], &fd) || !rs_parse_int(&argv[1], &maxBytes) || maxBytes < 0) {
    rs_set_error_text("bad arguments");
    return rs_set_result_text(result, "");
  }

  int timeout_ms = 0;
  if (argc >= 3) (void)rs_parse_int(&argv[2], &timeout_ms);

  if (timeout_ms > 0) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) {
      if (rc == 0) errno = ETIMEDOUT;
      rs_set_error(rc == 0 ? "recv timeout" : "poll");
      return rs_set_result_text(result, "");
    }
  }

  char *buf = (char*)malloc((size_t)maxBytes);
  if (!buf) {
    rs_set_error_text("malloc failed");
    return rs_set_result_text(result, "");
  }

  ssize_t n = recv(fd, buf, (size_t)maxBytes, 0);
  if (n < 0) {
    rs_set_error("recv");
    free(buf);
    return rs_set_result_text(result, "");
  }

  APIRET rc = rs_set_result_bin(result, buf, (size_t)n);
  free(buf);
  return rc;
}

/* RSClose(fd) -> 0 or -1 */
APIRET RSClose(const char *name, LONG argc, RXSTRING argv[],
                const char *queue, PRXSTRING result) {
  (void)name; (void)queue;
  rs_clear_error();

  if (argc < 1) return rs_set_result_int(result, -1);

  int fd = -1;
  if (!rs_parse_int(&argv[0], &fd)) {
    rs_set_error_text("bad fd");
    return rs_set_result_int(result, -1);
  }

  if (close(fd) != 0) {
    rs_set_error("close");
    return rs_set_result_int(result, -1);
  }
  return rs_set_result_int(result, 0);
}


/* RSUnloadFuncs() -> 0 on success
 * Deregisters all rexxsockets functions via RexxDeregisterFunction().
 * Intended as a minimal cleanup helper for interactive sessions/tests.
 */
APIRET RSUnloadFuncs(const char *name, LONG argc, RXSTRING argv[],
                     const char *queue, PRXSTRING result)
{
  (void)name; (void)argc; (void)argv; (void)queue;
  rs_clear_error();

  /* Core */
  (void)RexxDeregisterFunction("RSLoadFuncs");
  (void)RexxDeregisterFunction("RSUnloadFuncs");
  (void)RexxDeregisterFunction("RSVersion");
  (void)RexxDeregisterFunction("RSLastError");

  /* TCP */
  (void)RexxDeregisterFunction("RSTCPConnect");
  (void)RexxDeregisterFunction("RSTCPListen");
  (void)RexxDeregisterFunction("RSAccept");

  /* I/O */
  (void)RexxDeregisterFunction("RSSend");
  (void)RexxDeregisterFunction("RSRecv");
  (void)RexxDeregisterFunction("RSClose");

  /* Helpers */
  (void)RexxDeregisterFunction("RSSetBlocking");
  (void)RexxDeregisterFunction("RSPoll");

  /* UDP */
  (void)RexxDeregisterFunction("RSUDPBind");
  (void)RexxDeregisterFunction("RSUDPSend");
  (void)RexxDeregisterFunction("RSUDPRecv");

  /* Resolver */
  (void)RexxDeregisterFunction("RSResolve");

  return rs_set_result_int(result, 0);
}


/* RSLoadFuncs() -> 0 on success
 * Registers all rexxsockets functions via RexxRegisterFunctionExe().
 */
APIRET RSLoadFuncs(const char *name, LONG argc, RXSTRING argv[],
                   const char *queue, PRXSTRING result)
{
  (void)name; (void)argc; (void)argv; (void)queue;
  rs_clear_error();


  /* Allow unload */
  (void)RexxRegisterFunctionExe("RSUnloadFuncs", (RexxFunctionHandler*)RSUnloadFuncs);
  /* Core */
  (void)RexxRegisterFunctionExe("RSLoadFuncs", (RexxFunctionHandler*)RSLoadFuncs);
  (void)RexxRegisterFunctionExe("RSVersion",   (RexxFunctionHandler*)RSVersion);
  (void)RexxRegisterFunctionExe("RSLastError", (RexxFunctionHandler*)RSLastError);

  /* TCP */
  (void)RexxRegisterFunctionExe("RSTCPConnect", (RexxFunctionHandler*)RSTCPConnect);
  (void)RexxRegisterFunctionExe("RSTCPListen",  (RexxFunctionHandler*)RSTCPListen);
  (void)RexxRegisterFunctionExe("RSAccept",     (RexxFunctionHandler*)RSAccept);

  /* I/O */
  (void)RexxRegisterFunctionExe("RSSend",  (RexxFunctionHandler*)RSSend);
  (void)RexxRegisterFunctionExe("RSRecv",  (RexxFunctionHandler*)RSRecv);
  (void)RexxRegisterFunctionExe("RSClose", (RexxFunctionHandler*)RSClose);

  /* Helpers */
  (void)RexxRegisterFunctionExe("RSSetBlocking", (RexxFunctionHandler*)RSSetBlocking);
  (void)RexxRegisterFunctionExe("RSPoll",        (RexxFunctionHandler*)RSPoll);

  /* UDP */
  (void)RexxRegisterFunctionExe("RSUDPBind", (RexxFunctionHandler*)RSUDPBind);
  (void)RexxRegisterFunctionExe("RSUDPSend", (RexxFunctionHandler*)RSUDPSend);
  (void)RexxRegisterFunctionExe("RSUDPRecv", (RexxFunctionHandler*)RSUDPRecv);

  /* Resolver */
  (void)RexxRegisterFunctionExe("RSResolve", (RexxFunctionHandler*)RSResolve);

  return rs_set_result_int(result, 0);
}
