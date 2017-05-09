#! /usr/bin/env python
#
# Example program using irc.client.
#
# This program is free without restrictions; do anything you like with
# it.
#
# Joel Rosdahl <joel@rosdahl.net>

from __future__ import print_function

import zipfile
import rarfile

import os
import struct
import sys
import argparse
import shlex

import jaraco.logging

import irc.client

class DCCReceive(irc.client.SimpleIRCClient):
    def __init__(self, search):
        irc.client.SimpleIRCClient.__init__(self)
        self.received_bytes = 0
        self.users = set()
        self.search = search

    def on_ctcp(self, connection, event):
        payload = event.arguments[1]
        parts = shlex.split(payload)
        command, filename, peer_address, peer_port, size = parts
        if command != "SEND":
            return
        self.filename = os.path.basename(filename)
        if os.path.exists(self.filename):
            print("A file named", self.filename,
                "already exists. Refusing to save it.")
            self.connection.quit()
            return
        self.file = open(self.filename, "wb")
        peer_address = irc.client.ip_numstr_to_quad(peer_address)
        peer_port = int(peer_port)
        self.dcc = self.dcc_connect(peer_address, peer_port, "raw")

    def on_dccmsg(self, connection, event):
        data = event.arguments[0]
        self.file.write(data)
        self.received_bytes = self.received_bytes + len(data)
        self.dcc.send_bytes(struct.pack("!I", self.received_bytes))

    def on_dcc_disconnect(self, connection, event):
        self.file.close()
        print("Received file %s (%d bytes)." % (self.filename,
                                                self.received_bytes))
        try:
            zipped = zipfile.ZipFile(self.filename)
        except zipfile.BadZipFile as e:
            zipped = rarfile.RarFile(self.filename)

        getters = []
        for f in zipped.namelist():
            getters += [x for x in zipped.open(f).read().splitlines() if x.startswith('!')]

        for name in getters:
            n = name.split()[0][1:]
            if n in self.users:
                print ("sending", name)
                self.connection.privmsg("#bookz", name)
                return
        else:
            print ("no good things")

        # self.connection.quit()

    def on_disconnect(self, connection, event):
        sys.exit(0)

    def on_namreply(self, c, e):
        """
        e.arguments[0] == "@" for secret channels,
                          "*" for private channels,
                          "=" for others (public channels)
        e.arguments[1] == channel
        e.arguments[2] == nick list
        """

        ch_type, channel, nick_list = e.arguments

        if channel == '*':
            # User is not in any visible channel
            # http://tools.ietf.org/html/rfc2812#section-3.2.5
            return

        for nick in nick_list.split():
            nick_modes = []

            if nick[0] in self.connection.features.prefix:
                nick_modes.append(self.connection.features.prefix[nick[0]])
                nick = nick[1:]

            self.users.add(nick)

    def on_welcome(self, connection, event):
        print ("Connected")
        connection.join("#bookz")

    def on_join(self, connection, event):
        connection.privmsg("#bookz", "@search " + self.search)
        connection.names(["#bookz"])


def get_args():
    parser = argparse.ArgumentParser(
        description="Receive a single file to the current directory via DCC "
            "and then exit.",
    )
    parser.add_argument('server')
    parser.add_argument('nickname')
    parser.add_argument('search')
    parser.add_argument('-p', '--port', default=6667, type=int)
    jaraco.logging.add_arguments(parser)
    return parser.parse_args()

def main():
    args = get_args()
    jaraco.logging.setup(args)

    c = DCCReceive(args.search)
    try:
        c.connect(args.server, args.port, args.nickname)
    except irc.client.ServerConnectionError as x:
        print(x)
        sys.exit(1)

    # con.add_global_handler("welcome", on_connect)
    # con.add_global_handler("join", on_join)

    c.start()

if __name__ == "__main__":
    main()
