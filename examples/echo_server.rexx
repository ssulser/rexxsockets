/* examples/echo_server.rexx - poll-based TCP echo server
 *
 * Run:
 *   make
 *   regina examples/echo_server.rexx
 *
 * Test (another terminal):
 *   nc 127.0.0.1 8080
 */

call RXFuncAdd "RSLoadFuncs","rexxsockets","RSLoadFuncs"
if rc \= 0 then do
    say "Error loading Library"
    exit 16
end

call RSLoadFuncs
port = "8080"
listen = RSTCPListen("127.0.0.1", port, 64)
if listen = -1 then do
  say "listen failed:" RSLastError()
  exit 1
end

say "Echo server listening on 127.0.0.1:"port

clients.0 = 0

do forever
  /* build poll input stem: listen socket + all clients */
  in.0 = 1 + clients.0
  in.1.fd = listen
  in.1.events = "IN"

  do i = 1 to clients.0
    in.(i+1).fd = clients.i
    in.(i+1).events = "IN|HUP|ERR"
  end

  out. = ""
  rc = RSPoll("in.", 1000, "out.")
  if rc = -1 then do
    say "poll failed:" RSLastError()
    iterate
  end
  if rc = 0 then iterate

  do k = 1 to out.0
    fd = out.k.fd
    ev = out.k.revents

    if fd = listen then do
      peer. = ""
      c = RSAccept(listen, 0, "peer.")
      if c \= -1 then do
        clients.0 = clients.0 + 1
        clients.(clients.0) = c
        say "accepted" peer.host":"peer.port "fd="c
      end
      iterate
    end

    if pos("HUP", ev) > 0 | pos("ERR", ev) > 0 then do
      call DropClient fd
      iterate
    end

    data = RSRecv(fd, 4096, 0)
    if data = "" then do
      call DropClient fd
      iterate
    end

    n = RSSend(fd, data)
    if n = -1 then do
      say "send failed fd="fd":" RSLastError()
      call DropClient fd
    end
  end
end

DropClient:
  parse arg dead
  do i = 1 to clients.0
    if clients.i = dead then do
      call RSClose dead
      do j = i to clients.0-1
        clients.j = clients.(j+1)
      end
      clients.0 = clients.0 - 1
      say "closed fd="dead
      return
    end
  end
  return
