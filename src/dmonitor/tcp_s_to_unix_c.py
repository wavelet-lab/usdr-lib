#!/usr/bin/python3

# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

import argparse
import socket
import time

FILENAME = "usdr_debug_pipe"

# if os.path.exists(FILENAME):
#  os.remove(FILENAME)

parser = argparse.ArgumentParser(description='UNIX socket to ')
parser.add_argument('--host', dest='host', type=str, default="0.0.0.0",
                    help='TCP socket to bind')
parser.add_argument('--port', dest='port', type=int, default=7878,
                    help='TCP port to bind')
args = parser.parse_args()

HOST = args.host
PORT = args.port
print(f"TCP({HOST}:{PORT}) -> UNIX({FILENAME})")


def run():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # enable address reuse
        s.bind((HOST, PORT))
        s.listen()
        conn, addr = s.accept()
        with conn:
            print(f"TCP Connected by {addr}")
            unix = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            unix.connect(FILENAME)
            print("UNIX socket is connected!")

            while True:
                req = conn.recv(1024)
                print(f"Got REQ `{req}`")
                if not len(req):
                    raise Exception("Connection is broken")
                unix.send(req)
                rep = unix.recv(1024)
                print(f"Got REP `{rep}`")
                conn.send(rep)


while True:
    try:
        run()
    except KeyboardInterrupt:
        print("Interrupted")
        break
    except Exception as e:
        print("Restarting...", e)
        time.sleep(1)
