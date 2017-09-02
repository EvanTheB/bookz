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
import select

import jaraco.logging

import irc.client

class Bookzer(irc.client.SimpleIRCClient):
    def __init__(self, channel):
        super(Bookzer, self).__init__()
        self.users = set()
        self.channel = channel

    # def on_privmsg(self, c, e):
    #     print(e)

    # def on_pubmsg(self, c, e):
    #     print(e)

    def msg(self, msg):
        # connect -> search -> dcc recv -> choice list -> dcc recv
        #                ^\ -----------  </ ----------   </
        try:
            choices = [int(m) for m in msg.split()]
        except ValueError as e:
            print("searching", msg)
            self.connection.privmsg(self.channel, "@search " + msg)
        else:
            for choice in choices:
                print("requesting", self.getters[choice])
                self.connection.privmsg(self.channel, self.getters[choice])

    def on_ctcp(self, connection, event):
        payload = event.arguments[1]
        parts = shlex.split(payload)
        try:
            command, filename, peer_address, peer_port, size = parts
        except ValueError as e:
            print("failed to unpack:", parts)
        else:
            if command != "SEND":
                return
            filename = os.path.basename(filename)
            if os.path.exists(filename):
                print("A file named", filename, "already exists. Refusing to save it.")
                return
            print("downloading", filename, size)
            peer_address = irc.client.ip_numstr_to_quad(peer_address)
            peer_port = int(peer_port)

            dcc = self.dcc_connect(peer_address, peer_port, "raw")
            dcc.filename = filename
            dcc.file = open(filename, "wb")
            dcc.received_bytes = 0

    def on_dccmsg(self, connection, event):
        data = event.arguments[0]
        connection.file.write(data)
        connection.received_bytes = connection.received_bytes + len(data)
        connection.send_bytes(struct.pack("!I", connection.received_bytes))

    def on_dcc_disconnect(self, connection, event):
        connection.file.close()
        print("Received file %s (%d bytes)." % (connection.filename, connection.received_bytes))
        if connection.filename.startswith("Search"):
            try:
                zipped = zipfile.ZipFile(connection.filename)
            except zipfile.BadZipFile as e:
                zipped = rarfile.RarFile(connection.filename)

            getters = []
            for f in zipped.namelist():
                getters += [x for x in zipped.open(f).read().splitlines() if x.startswith('!')]

            self.getters = sorted([g for g in getters if g.split()[0][1:] in self.users])
            for i,g in enumerate(self.getters):
                print(i, g)
            print ("CHOOSE")

            zipped.close()
            os.remove(connection.filename)
        else:
            # got a book i guess
            if connection.filename.endswith('rar'):
                rarfile.RarFile(connection.filename).extractall()
                os.remove(connection.filename)
            if connection.filename.endswith('zip'):
                zipfile.ZipFile(connection.filename).extractall()
                os.remove(connection.filename)

    def on_disconnect(self, connection, event):
        print("disconnect")
        print(event)
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
        connection.join(self.channel)

    def on_join(self, connection, event):
        connection.names([self.channel])


def get_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('server')
    parser.add_argument('channel')
    parser.add_argument('nickname')
    parser.add_argument('-p', '--port', default=6667, type=int)
    jaraco.logging.add_arguments(parser)
    return parser.parse_args()

def main():
    args = get_args()
    jaraco.logging.setup(args)


    from jaraco.stream import buffer
    irc.client.ServerConnection.buffer_class = buffer.LenientDecodingLineBuffer

    c = Bookzer(args.channel)
    try:
        c.connect(args.server, args.port, args.nickname)
    except irc.client.ServerConnectionError as x:
        print(x)
        sys.exit(1)

    reactor = c.reactor

    while True:
        reactor.process_once(0.2)
        while sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
            line = sys.stdin.readline()
            if line:
                c.msg(line.strip())
            else: # an empty line means stdin has been closed
                print('eof')
                exit(0)


if __name__ == "__main__":
    main()
