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

class DCCReceive(irc.client.SimpleIRCClient):
    def __init__(self, channel):
        irc.client.SimpleIRCClient.__init__(self)
        self.received_bytes = 0
        self.users = set()
        self.channel = channel
        self.expect_book = False

    def msg(self, msg):
        # connect -> search -> dcc recv -> choice list -> dcc recv  
        #                ^\ -----------  </ ----------   </  
        if self.expect_book:
            try:
                choice = int(msg)
            except ValueError as e:
                print("searching", msg)
                self.expect_book = False
                self.connection.privmsg(self.channel, "@search " + msg)
            else:
                print("sending", self.getters[choice])
                self.connection.privmsg(self.channel, self.getters[choice])
        else:
            print("searching", msg)
            self.connection.privmsg(self.channel, "@search " + msg)

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
            self.filename = os.path.basename(filename)
            if os.path.exists(self.filename):
                print("A file named", self.filename, "already exists. Refusing to save it.")
                return
            print("downloading", self.filename, size)
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
        print("Received file %s (%d bytes)." % (self.filename, self.received_bytes))
        if not self.expect_book:
            try:
                zipped = zipfile.ZipFile(self.filename)
            except zipfile.BadZipFile as e:
                zipped = rarfile.RarFile(self.filename)

            getters = []
            for f in zipped.namelist():
                getters += [x for x in zipped.open(f).read().splitlines() if x.startswith('!')]

            self.getters = [g for g in getters if g.split()[0][1:] in self.users]
            for i,g in enumerate(self.getters):
                print(i, g)
            print ("CHOOSE")
            self.expect_book = True

            zipped.close()
        else:
            # got a book i guess
            if self.filename.endswith('rar'):
                rarfile.RarFile(self.filename).extractall()
                os.remove(self.filename)
            if self.filename.endswith('zip'):
                zipfile.ZipFile(self.filename).extractall()
                os.remove(self.filename)
            
            self.expect_book = False

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

    c = DCCReceive(args.channel)
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
