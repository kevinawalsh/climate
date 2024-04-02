#!/usr/bin/env python3

# Author: K. Walsh <kwalsh@cs.holycross.edu>
# Date: 23 March 2013
#
# A web server for climate data, in Python. Run it like this:
#   ./climate-server.py
# or:
#   python3 ./climate-server.py
#
# By default, it will listen on port 8888, and serve files from the ROOT
# directory defined below. These caan be changed by command line options, e.g.
#   ./climate-server.py 8080 /var/www/some/other/directory
#
# For path=/status, the server sends:
#    time: 13:45
#    date: 3/20/2024
#    daytime: 8:15 21:30
#    mode: auto | manual rgbx rgbx rgbx | simulate mm/dd hh:mm
#    historical: mm/dd baseline max1950 max1951 ...
#
# For path=/monitor, the server sends the same, but keeps the connection open
# and re-sends any time there is an update.

from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
import threading
import time, os, sys, errno
from datetime import datetime
from pytz import timezone
from base64 import b64encode
import csv

import arduino_secrets

PORT = 8888
ROOT = "/var/www/kwalsh/climate"

# see arduino_secrets.py for USER and PASS, like:
# USER = "something"
# PASS = "whatever"

AUTH = 'Basic ' + b64encode(f"{USER}:{PASS}".encode('utf-8')).decode("ascii")

tz = timezone('US/Eastern')

# Get command-line parameters, if present
if len(sys.argv) >= 2:
    PORT = int(sys.argv[1])
if len(sys.argv) >= 3:
    ROOT = sys.argv[2]
ROOT = os.path.normpath(ROOT + '/')

os.chdir(ROOT)

month_offset = [ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 ]
# def get_historical_temp(date):
#     daynum = month_offset[date.month-1] + date.day;
#     # daynum,min,max,min50s,max50s,min60s,max60s,min70s,max70
#     temp = [ "0.0" ] * 9
#     with open('daily.csv', newline='') as csvfile:
#         data = csv.reader(csvfile, delimiter=',')
#         for row in data:
#             if len(row) < 9 or not row[0].isdigit():
#                 continue
#             if int(row[0]) == daynum:
#                 temp = row[1:10]
#     return " ".join([("%.1f" % (float(x))) for x in temp ])

class State:
    def __init__(self):
        self.version = 1
        self.date = ""
        self.mode = "auto"
        # self.temp = "0 0 0 0 0 0 0 0"
        self.updates = threading.Condition()

    def update(self, msg):
        with self.updates:
            for line in msg.splitlines():
                print(line)
                (key, val) = line.split('=' , 1)
                if key == "mode":
                    self.mode = val
            self.version = self.version + 1
            self.updates.notify_all()

    def get(self):
        with self.updates:
            now = datetime.now(tz)
            # temp = get_historical_temp(now.date())
            data = [
                    "time: " + now.strftime('%H:%M'),
                    "date: " + now.strftime('%m/%d/%Y'),
                    "daytime: 8:00 21:00",
                    "mode: " + self.mode
                    ] # "historical: " + temp
            return (self.version, data)

    def waitfor(self, nextver):
        with self.updates:
            while self.version < nextver:
                self.updates.wait(30.0)
            return self.get()

state = State()

class Handler(BaseHTTPRequestHandler):
    def do_HEAD(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()

    def do_AUTHHEAD(self):
        self.send_response(401)
        self.send_header(f'WWW-Authenticate', 'Basic realm=\"{USER}\"')
        self.send_header('Content-type', 'text/plain')
        self.end_headers()

    def do_PUT(self):
        if self.headers['authorization'] == None:
            self.do_AUTHHEAD()
            self.wfile.write(b'no auth header received\n')
            return
        elif self.headers['Authorization'] == AUTH:
            self.do_HEAD()
            length = int(self.headers['content-length'])
            msg = self.rfile.read(length).decode()
            state.update(msg)
            (ver, lines) = state.get()
            for line in lines:
                self.wfile.write(str(line).encode() + b'\n')
            return
        else:
            self.do_AUTHHEAD()
            self.wfile.write(b'not authenticated\n')
            return

    do_POST = do_PUT

    def do_GET_file(self, filename):
        try:
            with open(filename, 'rb') as f:
                data = f.read()
        except Exception:
            data = None
        if data:
            self.do_HEAD()
            self.wfile.write(data)
        else:
            self.send_response(404)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(b'not found\n')

    def do_GET_status(self, continuous):
        self.do_HEAD()
        (ver, lines) = state.get()
        for line in lines:
            self.wfile.write(str(line).encode() + b'\n')
        while continuous:
            (ver, lines) = state.waitfor(ver+1)
            for line in lines:
                self.wfile.write(str(line).encode() + b'\n')

    def do_GET(self):
        try:
            if self.path == "/daily.csv":
                self.do_GET_file("daily.csv")
            elif self.path in [ "/", "/status" ]:
                self.do_GET_status(False)
            elif self.path == "/monitor" :
                self.do_GET_status(True)
        except IOError as e:
           if e.errno == errno.EPIPE:
               # print("Disconnect")
               pass
           else:
               raise

class ThreadingSimpleServer(ThreadingMixIn, HTTPServer):
    pass

def run():
    print("Serving files from ", ROOT)
    print("Serving at network port", PORT)
    print("Current status:")
    print("  " + "\n  ".join(state.get()[1]))
    server = ThreadingSimpleServer(("", PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()
        print("Server closed")

if __name__ == '__main__':
    run()
