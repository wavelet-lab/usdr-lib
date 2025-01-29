#!/usr/bin/python3

# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

import argparse
import os
import os.path
import socket
import time

FILENAME = "usdr_debug_pipe"

parser = argparse.ArgumentParser(description='UNIX socket to ')
parser.add_argument('--host', dest='host', type=str, default="0.0.0.0",
                    help='TCP socket to connect')
parser.add_argument('--port', dest='port', type=int, default=7878,
                    help='TCP port to connect')
args = parser.parse_args()

HOST = args.host
PORT = args.port
print(f"UNIX({FILENAME}) -> TCP({HOST}:{PORT})")


def clean():
    if os.path.exists(FILENAME):
        os.remove(FILENAME)


clean()


def run():
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        # s.bind((HOST, PORT))
        s.bind(FILENAME)
        s.listen()
        conn, addr = s.accept()
        with conn:
            print(f"UNIX Accepted by {addr}")
            u = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            # unix.connect(FILENAME)
            u.connect((HOST, PORT))
            print("TCP socket is ok")

            while True:
                req = conn.recv(1024)
                print(f"Got REQ `{req}`")
                if not len(req):
                    raise Exception("Connection is broken")
                u.send(req)
                rep = u.recv(1024)
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
        time.sleep(0.3)
    finally:
        clean()
