/* test/tcp_client_local.rexx
 *
 * Local loopback TCP client test.
 * Requires a server listening on 127.0.0.1:8080
 * (e.g. examples/echo_server.rexx).
 */
call RxFuncAdd "RSLoadFuncs","rexxsockets","RSLoadFuncs"
call RSLoadFuncs

fd = RSTCPConnect("127.0.0.1", "8080", 3000)
if fd = -1 then do
  say "connect failed:" RSLastError()
  exit 1
end

msg = "hello local"||'0d0a'x
n = RSSend(fd, msg)
if n = -1 then say "send failed:" RSLastError()

data = RSRecv(fd, 4096, 3000)
if data = "" then
  say "recv failed:" RSLastError()
else
  say "received:" data

call RSClose fd
exit 0
