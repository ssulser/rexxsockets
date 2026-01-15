/* examples/udp_echo.rexx - simple UDP echo
 *
 * Terminal 1:
 *   make
 *   regina examples/udp_echo.rexx
 *
 * Terminal 2:
 *   echo "hi" | nc -u 127.0.0.1 9999
 */
call load "test/loadfuncs.rexx"

udp = RSUDPBind("127.0.0.1", "9999")
if udp = -1 then do
  say "udp bind failed:" RSLastError()
  exit 1
end

say "UDP echo on 127.0.0.1:9999"

do forever
  peer. = ""
  data = RSUDPRecv(udp, 2048, 0, "peer.")
  if data = "" then do
    say "recv:" RSLastError()
    iterate
  end
  say "from" peer.host":"peer.port "->" data
  n = RSUDPSend(udp, peer.host, peer.port, data)
  if n = -1 then say "send:" RSLastError()
end
