"""End-to-end OpenRouter wire contract against a local HTTP/SSE server; no paid calls."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

cli=Path(sys.argv[1]).resolve()
core=cli.with_name("nextagent.exe" if os.name=="nt" else "nextagent")
base=cli.parent/("http-test-"+str(time.time_ns()))
project=base/"project"
state=base/"state"
class Native:
 def __init__(self,root):self.p=subprocess.Popen([str(core),"--root",str(root),"serve","--no-gate"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding="utf-8")
 def call(self,method,params):
  self.p.stdin.write(json.dumps({"jsonrpc":"2.0","id":1,"method":method,"params":params})+"\n");self.p.stdin.flush();reply=json.loads(self.p.stdout.readline());assert "error"not in reply,reply;return reply["result"]
 def close(self):self.p.stdin.close();assert self.p.wait(timeout=20)==0
native=Native(project)
native.call("patch",{"edits":[{"path":"tests/test_hello.py","new":"import unittest\nfrom hello import hello\nclass TestHello(unittest.TestCase):\n    def test_value(self): self.assertEqual(hello(), 1)\n","create":True}]})
native.close()
errors=[]
class Server(http.server.BaseHTTPRequestHandler):
 count=0
 def log_message(self,*args):pass
 def do_POST(self):
  try:
   assert self.path=="/api/v1/chat/completions"
   assert self.headers["Authorization"]=="Bearer harness-test-key"
   request=json.loads(self.rfile.read(int(self.headers["Content-Length"])))
   assert request["stream"] and any(t["function"]["name"]=="run_check" for t in request["tools"])
   count=type(self).count;type(self).count+=1
   self.send_response(200);self.send_header("Content-Type","text/event-stream");self.end_headers()
   chunks=[]
   if count==0:
    args=json.dumps({"edits":[{"path":"hello.py","new":"def hello():\n    return 1\n","create":True}]})
    chunks=[{"choices":[{"delta":{"tool_calls":[{"index":0,"id":"native_","function":{"name":"pa","arguments":args[:12]}}]}}]},
     {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"patch","function":{"name":"tch","arguments":args[12:]}}]},"finish_reason":"tool_calls"}],"usage":{"prompt_tokens":100,"completion_tokens":10,"cost":0.001}}]
   elif count==1:
    assert request["messages"][-1]["tool_call_id"]=="native_patch"
    assert json.loads(request["messages"][-1]["content"])["paths"]==["hello.py"]
    chunks=[{"choices":[{"delta":{"tool_calls":[{"index":0,"id":"check","function":{"name":"run_check","arguments":json.dumps({"kind":"python","timeout_ms":10000})}}]},"finish_reason":"tool_calls"}],"usage":{"prompt_tokens":120,"completion_tokens":10}}]
   else:
    result=json.loads(request["messages"][-1]["content"])
    assert result["exit_code"]==0 and "Ran 1 test" in result["output"],result
    chunks=[{"choices":[{"delta":{"content":"Готово: код записан и тест пройден."},"finish_reason":"stop"}],"usage":{"prompt_tokens":140,"completion_tokens":12,"cost":0.002,"prompt_tokens_details":{"cached_tokens":40}}}]
   self.wfile.write(b": keepalive\n\n")
   for chunk in chunks:
    frame=("data: "+json.dumps(chunk,ensure_ascii=False)+"\n\n").encode()
    for i in range(0,len(frame),7):self.wfile.write(frame[i:i+7]);self.wfile.flush()
   self.wfile.write(b"data: [DONE]\n\n");self.wfile.flush()
  except Exception as e:errors.append(repr(e))
server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Server)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
try:
 run=subprocess.run([str(cli),"--root",str(project),"--state-dir",str(state),"--prompt","Implement hello and test it","--endpoint",f"http://127.0.0.1:{server.server_port}/api/v1/chat/completions","--json"],env=dict(os.environ,OPENROUTER_API_KEY="harness-test-key"),capture_output=True,text=True,encoding="utf-8",timeout=40)
 assert not errors,errors
 assert run.returncode==0,(run.stdout,run.stderr)
 result=json.loads(run.stdout);assert result["tool_calls"]==2 and result["input_tokens"]==360 and result["output_tokens"]==32,result
 assert "harness-test-key" not in run.stdout
 native=Native(project);assert "return 1" in native.call("view",{"target":"hello.py"})["text"];native.close()
 print("OpenRouter wire test: fragmented SSE/tool arguments, native patch, real Python test, metrics and key redaction passed")
finally:server.shutdown();server.server_close();thread.join(timeout=5)

# HTTP errors must not persist or print the bearer key even if the provider echoes it.
class Unauthorized(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_POST(self):
  self.rfile.read(int(self.headers["Content-Length"]))
  self.send_response(401);self.end_headers();self.wfile.write(b'{"error":{"message":"bad harness-test-key"}}')
error_server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Unauthorized)
error_thread=threading.Thread(target=error_server.serve_forever,daemon=True);error_thread.start()
try:
 failed=subprocess.run([str(cli),"--root",str(project),"--state-dir",str(state),"--prompt","Expected HTTP failure","--endpoint",f"http://127.0.0.1:{error_server.server_port}/api/v1/chat/completions","--json"],env=dict(os.environ,OPENROUTER_API_KEY="harness-test-key"),capture_output=True,text=True,encoding="utf-8",timeout=20)
 assert failed.returncode==1
 assert "401" in failed.stdout and "harness-test-key" not in failed.stdout+failed.stderr,failed.stdout
 print("OpenRouter HTTP 401 and bearer-key redaction passed")
finally:error_server.shutdown();error_server.server_close();error_thread.join(timeout=5)
