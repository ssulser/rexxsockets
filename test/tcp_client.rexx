/* test/tcp_client.rexx - tiny HTTP client demo */
call RxFuncAdd "RSLoadFuncs","rexxsockets","RSLoadFuncs"
call RSLoadFuncs

fd = RSTCPConnect("example.com", "80", 3000)
if fd = -1 then do
  say "connect failed:" RSLastError()
  exit 1
end

req = "GET / HTTP/1.0"||'0d0a'x||"Host: example.com"||'0d0a'x||'0d0a'x
n = RSSend(fd, req)
if n = -1 then say "send failed:" RSLastError()

data = RSRecv(fd, 4096, 3000)
if data = "" then say "recv:" RSLastError()
else say data

call RSClose fd
call RSUnloadFuncs
exit 0
