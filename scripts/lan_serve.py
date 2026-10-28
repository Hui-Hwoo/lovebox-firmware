#!/usr/bin/env python3
# 局域网固件服务器(更新方式3):在 :8080 的 /fw/firmware.bin 提供固件,
# 设备每 LB_OTA_POLL_S 秒轮询它,版本不同就自动拉取升级。需设备在家里网。
#
#   用法:  python3 scripts/lan_serve.py [firmware.bin路径]
#   默认固件: build/lovebox_fw.bin
#   把本机 IP 填进 main/lovebox_config.h 的 LB_SERVER_IP(或设备网页里相应设置)。
import sys, http.server, socketserver

FW   = sys.argv[1] if len(sys.argv) > 1 else "build/lovebox_fw.bin"
PORT = 8080

class H(http.server.BaseHTTPRequestHandler):
    def _fw(self, body=True):
        try:
            data = open(FW, "rb").read()
        except OSError as e:
            self.send_response(404); self.end_headers(); print("找不到固件:", e); return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if body: self.wfile.write(data)
    def do_GET(self):
        if self.path.startswith("/fw/firmware.bin"): self._fw(True)
        else: self.send_response(200); self.end_headers()
    def do_HEAD(self):
        if self.path.startswith("/fw/firmware.bin"): self._fw(False)
        else: self.send_response(200); self.end_headers()
    def do_POST(self):            # 设备可能的 /checkin 等,回 200 即可
        self.send_response(200); self.end_headers()
    def log_message(self, *a):    # 精简日志
        print("%s %s" % (self.command, self.path))

socketserver.TCPServer.allow_reuse_address = True
print("服务固件 %s 于 http://<本机IP>:%d/fw/firmware.bin  (Ctrl-C 停止)" % (FW, PORT))
socketserver.TCPServer(("", PORT), H).serve_forever()
