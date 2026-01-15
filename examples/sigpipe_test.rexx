/* test/sigpipe_test.rexx - ensure RSSend won't terminate via SIGPIPE */
call load "test/loadfuncs.rexx"

port = "9099"

say "Starting listener..."
listen = RSTCPListen("127.0.0.1", port, 4)
if listen = -1 then do
  say "listen failed:" RSLastError()
  exit 1
end

/* client connects */
fd = RSTCPConnect("127.0.0.1", port, 2000)
if fd = -1 then do
  say "connect failed:" RSLastError()
  call RSClose listen
  exit 1
end

/* server accepts and closes immediately */
peer. = ""
c = RSAccept(listen, 2000, "peer.")
if c = -1 then do
  say "accept failed:" RSLastError()
  call RSClose fd
  call RSClose listen
  exit 1
end
say "accepted from" peer.host":"peer.port
call RSClose c
say "server closed connection"

/* client tries to send -> must NOT crash */
n = RSSend(fd, "hello"||'0d0a'x)
if n = -1 then
  say "send failed (ok):" RSLastError()
else
  say "send ok bytes="n

call RSClose fd
call RSClose listen
say "done"
exit 0
