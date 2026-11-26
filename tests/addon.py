"""Exercise the real HTTP worker, UI navigation and profile changes on loopback."""
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from threading import Thread
import json
import os
import subprocess
import sys

class Addon(BaseHTTPRequestHandler):
    def do_GET(self):
        meta={"type":"movie","id":"fixture:1","name":"Fixture film","description":"A test title"}
        paths={
            "/manifest.json":{"id":"test.notflix","name":"Fixture","types":["movie"],"resources":["catalog","meta","stream"],
              "catalogs":[{"type":"movie","id":"top","name":"Films","extra":[{"name":"search"}]}]},
            "/catalog/movie/top.json":{"metas":[meta]},
            "/catalog/movie/top/search=test%20%2F%20show.json":{"metas":[meta]},
            "/meta/movie/fixture%3A1.json":{"meta":meta},
            "/stream/movie/fixture%3A1.json":{"streams":[{"name":"Broken scheme","url":"invalid:"},
              {"name":"Stream","url":f"http://127.0.0.1:{self.server.server_port}/media.mp4"}]}}
        payload=paths.get(self.path)
        if payload is None:
            self.send_error(404)
            return
        data=json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type","application/json")
        self.send_header("Content-Length",str(len(data)))
        self.end_headers()
        self.wfile.write(data)

with ThreadingHTTPServer(("127.0.0.1",0),Addon) as server:
    worker=Thread(target=server.serve_forever,daemon=True)
    worker.start()
    env=dict(os.environ,SDL_VIDEODRIVER="dummy",NOTFLIX_TEST_MANIFEST=f"http://127.0.0.1:{server.server_port}/manifest.json")
    try:
        subprocess.run([sys.argv[1],"--self-test"],cwd=sys.argv[2],env=env,check=True,timeout=30)
    finally:
        server.shutdown()
        worker.join()

