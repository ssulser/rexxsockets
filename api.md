# API Referenz – rexxsockets

Dieses Dokument beschreibt die in **Regina Rexx** verwendbaren Funktionen der Library **rexxsockets**.

## Allgemeines

- Alle Funktionen setzen intern einen „last error“-Text.  
  Den kannst du mit `RS_LastError()` abrufen.
- Rückgabewerte:
  - Funktionen, die File Descriptors liefern, geben bei Fehlern `-1` zurück.
  - Funktionen, die Daten liefern (`RS_Recv`, `RS_UDPRecv`) geben bei Fehlern/Timeout `""` zurück.
- Timeouts:
  - Wenn ein Timeout passiert, wird `RS_LastError()` gesetzt (z.B. `"recv timeout: (60) Operation timed out"`).

## Laden der Funktionen

Beispiel `load.rexx` (macOS):

```rexx
lib = "./librexxsockets.dylib"
/* Linux: lib = "./librexxsockets.so" */

call rxfuncadd "RS_Version",     lib, "RS_Version"
call rxfuncadd "RS_LastError",   lib, "RS_LastError"
call rxfuncadd "RS_TCPConnect",  lib, "RS_TCPConnect"
call rxfuncadd "RS_TCPListen",   lib, "RS_TCPListen"
call rxfuncadd "RS_Accept",      lib, "RS_Accept"
call rxfuncadd "RS_SetBlocking", lib, "RS_SetBlocking"
call rxfuncadd "RS_Poll",        lib, "RS_Poll"
call rxfuncadd "RS_UDPBind",     lib, "RS_UDPBind"
call rxfuncadd "RS_UDPSend",     lib, "RS_UDPSend"
call rxfuncadd "RS_UDPRecv",     lib, "RS_UDPRecv"
call rxfuncadd "RS_Resolve",     lib, "RS_Resolve"
call rxfuncadd "RS_Send",        lib, "RS_Send"
call rxfuncadd "RS_Recv",        lib, "RS_Recv"
call rxfuncadd "RS_Close",       lib, "RS_Close"

say RS_Version()
return
```

---

# Funktionsreferenz

## RS_Version()

**Signatur**

```rexx
v = RS_Version()
```

**Beschreibung**  
Gibt eine Versionszeichenkette der Library zurück.

**Rückgabe**  
String, z.B. `"rexxsockets 0.2"`.

---

## RS_LastError()

**Signatur**

```rexx
e = RS_LastError()
```

**Beschreibung**  
Gibt den zuletzt gesetzten Fehlertext zurück.

**Rückgabe**  
String (kann leer sein).

---

## RS_TCPListen(bindHost, service [, backlog])

**Signatur**

```rexx
listenFd = RS_TCPListen(bindHost, service, backlog)
```

**Parameter**
- `bindHost` (String): Host/IP zum Binden.  
  Leerer String bedeutet „alle Interfaces“ (entspricht `NULL` für `getaddrinfo` mit `AI_PASSIVE`).
- `service` (String): Port oder Servicename (z.B. `"8080"`, `"http"`).
- `backlog` (Integer, optional): Listen-Backlog. Default: `64`.

**Rückgabe**
- `listenFd` (Integer FD) bei Erfolg
- `-1` bei Fehler (Details: `RS_LastError()`)

---

## RS_Accept(listenFd [, timeout_ms] [, stemName])

**Signatur**

```rexx
peer. = ""
fd = RS_Accept(listenFd, timeout_ms, "peer.")
```

**Parameter**
- `listenFd` (Integer): FD von `RS_TCPListen()`.
- `timeout_ms` (Integer, optional): Timeout in Millisekunden. `0` bedeutet „blockierend akzeptieren“.
- `stemName` (String, optional): Stem, in den Peer-Infos geschrieben werden.

**Stem Output (wenn `stemName` gesetzt)**
- `peer.host` – Host/IP
- `peer.port` – Port/Service als String

**Rückgabe**
- neuer Client-FD bei Erfolg
- `-1` bei Fehler (Details: `RS_LastError()`)

---

## RS_TCPConnect(host, service [, timeout_ms])

**Signatur**

```rexx
fd = RS_TCPConnect(host, service, timeout_ms)
```

**Parameter**
- `host` (String): Zielhost oder IP
- `service` (String): Port/Service
- `timeout_ms` (Integer, optional): Timeout fürs Connect in Millisekunden. Default: `0` (OS-Default / blockierend).

**Rückgabe**
- FD bei Erfolg
- `-1` bei Fehler

---

## RS_Send(fd, data [, flags])

**Signatur**

```rexx
n = RS_Send(fd, data)
```

**Parameter**
- `fd` (Integer): Socket-FD
- `data` (String): zu sendende Bytes
- `flags` (Integer, optional): Flags für `send()`. Default: `0`  
  *Hinweis:* Auf Linux wird intern (wenn verfügbar) `MSG_NOSIGNAL` OR-ed, um SIGPIPE zu vermeiden.

**Rückgabe**
- Anzahl gesendeter Bytes (Integer) bei Erfolg
- `-1` bei Fehler

---

## RS_Recv(fd, maxBytes [, timeout_ms])

**Signatur**

```rexx
data = RS_Recv(fd, maxBytes, timeout_ms)
```

**Parameter**
- `fd` (Integer): Socket-FD
- `maxBytes` (Integer): maximale Anzahl Bytes
- `timeout_ms` (Integer, optional): Timeout in ms.  
  Wenn Timeout eintritt, liefert die Funktion `""` und setzt `RS_LastError()` (z.B. `"recv timeout: ..."`).

**Rückgabe**
- empfangene Bytes als String (kann Binärdaten enthalten)
- `""` bei Fehler/Timeout (Details: `RS_LastError()`)

---

## RS_Close(fd)

**Signatur**

```rexx
rc = RS_Close(fd)
```

**Beschreibung**  
Schließt den FD (Socket).

**Rückgabe**
- `0` bei Erfolg
- `-1` bei Fehler

---

## RS_SetBlocking(fd, blocking)

**Signatur**

```rexx
rc = RS_SetBlocking(fd, 0)  /* non-blocking */
rc = RS_SetBlocking(fd, 1)  /* blocking */
```

**Parameter**
- `fd` (Integer)
- `blocking` (Integer): `0` = non-blocking, `1` = blocking  
  Implementiert über `fcntl(F_GETFL/F_SETFL)` und `O_NONBLOCK`.

**Rückgabe**
- `0` bei Erfolg
- `-1` bei Fehler

---

## RS_Poll(inStem, timeout_ms, outStem)

Multiplexing für mehrere FDs via `poll()`.

**Signatur**

```rexx
/* Input */
in.0 = 2
in.1.fd = listenFd
in.1.events = "IN"
in.2.fd = clientFd
in.2.events = "IN|OUT"

/* Call */
rc = RS_Poll("in.", 5000, "out.")
```

**Input-Stem Format (`inStem`)**
- `in.0` = Anzahl Einträge `N`
- Für i=1..N:
  - `in.i.fd` = Integer FD
  - `in.i.events` = String der gewünschten Events:
    - `"IN"`  (POLLIN)
    - `"OUT"` (POLLOUT)
    - `"ERR"` (POLLERR)
    - Kombinationen mit `|`, z.B. `"IN|OUT"`

Wenn `in.i.events` fehlt, wird default `"IN"` verwendet.

**Output-Stem Format (`outStem`)**
- `out.0` = Rückgabewert von `poll()` (Anzahl fds mit Events, 0 bei Timeout)
- Für jedes FD mit `revents != 0` wird ein Eintrag erzeugt:
  - `out.k.fd` = FD
  - `out.k.revents` = String der tatsächlich anstehenden Events (z.B. `"IN"`, `"HUP"`, `"IN|HUP"`, ...)

**Rückgabe**
- `rc` = Rückgabewert von `poll()`:
  - `>0` Anzahl fds mit Events
  - `0` Timeout
  - `-1` Fehler (Details: `RS_LastError()`)

---

## RS_UDPBind(bindHost, service)

**Signatur**

```rexx
udpFd = RS_UDPBind(bindHost, service)
```

**Parameter**
- `bindHost` (String): Host/IP zum Binden (leer = alle Interfaces)
- `service` (String): Port/Service

**Rückgabe**
- FD bei Erfolg
- `-1` bei Fehler

---

## RS_UDPSend(fd, host, service, data)

**Signatur**

```rexx
n = RS_UDPSend(udpFd, "127.0.0.1", "9999", "hello")
```

**Parameter**
- `fd` (Integer): UDP-Socket (typischerweise von `RS_UDPBind`)
- `host` (String): Zielhost/IP
- `service` (String): Zielport
- `data` (String): Bytes

**Rückgabe**
- Bytes gesendet
- `-1` bei Fehler

---

## RS_UDPRecv(fd, maxBytes [, timeout_ms] [, stemName])

**Signatur**

```rexx
peer. = ""
data = RS_UDPRecv(udpFd, 2048, 5000, "peer.")
```

**Parameter**
- `fd` (Integer): UDP-Socket FD
- `maxBytes` (Integer): maximale Bytes
- `timeout_ms` (Integer, optional): Timeout ms (0 = blockierend)
- `stemName` (String, optional): Stem für Sender-Infos

**Stem Output (wenn `stemName` gesetzt)**
- `peer.host` – Host/IP des Senders
- `peer.port` – Port/Service des Senders

**Rückgabe**
- empfangene Bytes als String
- `""` bei Fehler/Timeout (Details: `RS_LastError()`)

---

## RS_Resolve(host, service, stemName, family, socktype [, protocol])

Wrapper um `getaddrinfo()` mit Rückgabe als Rexx-Stem.

**Signatur**

```rexx
res. = ""
rc = RS_Resolve("example.com", "80", "res.", 0, 1, 6)
```

**Parameter**
- `host` (String): Hostname/IP (kann leer sein, je nach Verwendung)
- `service` (String): Port/Service
- `stemName` (String): Ziel-Stem
- `family` (Integer): z.B. `0` (AF_UNSPEC), `2` (AF_INET), `30` (AF_INET6)
- `socktype` (Integer): z.B. `1` (SOCK_STREAM), `2` (SOCK_DGRAM)
- `protocol` (Integer, optional): z.B. `6` (IPPROTO_TCP), `17` (IPPROTO_UDP). Default: `0`.

**Output-Stem**
- `res.0` = Anzahl Einträge
- Für i=1..N:
  - `res.i.host`
  - `res.i.port`
  - `res.i.family`
  - `res.i.socktype`
  - `res.i.protocol`

**Rückgabe**
- `0` bei Erfolg
- `-1` bei Fehler (Details: `RS_LastError()`)

---

# Testprogramme

## sigpipe_test.rexx

Ziel: sicherstellen, dass `RS_Send()` den Interpreter **nicht** durch SIGPIPE beendet.

Ablauf (typisch):
1. `RS_TCPListen()`
2. Client verbindet per `RS_TCPConnect()`
3. Server macht `RS_Accept()` und schließt den neuen FD sofort (`RS_Close()`)
4. Client macht `RS_Send()` und bekommt entweder:
   - `-1` mit `RS_LastError()` (`Broken pipe` / `Connection reset`) **oder**
   - in seltenen Timing-Fällen noch einen Erfolg
5. Wichtig: **kein Crash**.

## load.rexx

Ziel: alle External Functions via `rxfuncadd` registrieren.  
Du kannst es in jedem Rexx-Testscript mit

```rexx
call load "load.rexx"
```

einbinden.

---

# Hinweise zur Portabilität

## NI_MAXHOST / NI_MAXSERV Fallback

Falls dein SDK `NI_MAXHOST` / `NI_MAXSERV` nicht definiert, verwende diesen Patch direkt nach `#include <netdb.h>`:

```c
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif
```

## SIGPIPE Mitigation

- macOS/BSD: `SO_NOSIGPIPE` pro Socket
- Linux: `MSG_NOSIGNAL` bei `send()` (wenn verfügbar)

