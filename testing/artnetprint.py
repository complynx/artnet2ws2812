#!/usr/bin/env python3
# coding=utf-8

import socket
import struct
import time
import netifaces
import zlib
import threading
import colorsys
import argparse
import fnmatch
import sys

import winreg as wr

def get_connection_name_from_guid(iface_guids):
    iface_names = ['(unknown)' for i in range(len(iface_guids))]
    reg = wr.ConnectRegistry(None, wr.HKEY_LOCAL_MACHINE)
    reg_key = wr.OpenKey(reg, r'SYSTEM\\CurrentControlSet\\Control\\Network\\{4d36e972-e325-11ce-bfc1-08002be10318}')
    for i in range(len(iface_guids)):
        try:
            reg_subkey = wr.OpenKey(reg_key, iface_guids[i] + r'\\Connection')
            iface_names[i] = wr.QueryValueEx(reg_subkey, 'Name')[0]
        except FileNotFoundError:
            pass
    return iface_names

def get_driver_name_from_guid(iface_guids):
    iface_names = ['(unknown)' for i in range(len(iface_guids))]
    reg = wr.ConnectRegistry(None, wr.HKEY_LOCAL_MACHINE)
    reg_key = wr.OpenKey(reg, r'SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}')
    for i in range(wr.QueryInfoKey(reg_key)[0]):
        subkey_name = wr.EnumKey(reg_key, i)
        try:
            reg_subkey = wr.OpenKey(reg_key, subkey_name)
            guid = wr.QueryValueEx(reg_subkey, 'NetCfgInstanceId')[0]
            try:
                idx = iface_guids.index(guid)
                iface_names[idx] = wr.QueryValueEx(reg_subkey, 'DriverDesc')[0]
            except ValueError:
                pass
        except FileNotFoundError:
            pass
        except PermissionError:
            pass
    return iface_names

def get_addr(pattern):
    ifs = netifaces.interfaces()
    devs = get_driver_name_from_guid(ifs)
    cons = get_connection_name_from_guid(ifs)
    ifs_w = zip(ifs, devs, cons)

    for ifw in ifs_w:
        if_addrs = netifaces.ifaddresses(ifw[0])
        if netifaces.AF_INET in if_addrs:
            for if_addr in if_addrs[netifaces.AF_INET]:
                if len(fnmatch.filter(ifw, pattern)) > 0 or fnmatch.fnmatch(if_addr['addr'], pattern):
                    return if_addr['addr'], if_addr['broadcast'] if 'broadcast' in if_addr else if_addr['addr']

def print_networks():
    ifs = netifaces.interfaces()
    devs = get_driver_name_from_guid(ifs)
    cons = get_connection_name_from_guid(ifs)
    ifs_w = zip(ifs, devs, cons)

    for ifw in ifs_w:
        if_addrs = netifaces.ifaddresses(ifw[0])
        if netifaces.AF_INET in if_addrs:
            print(f"Interface: {ifw[2]}\nUID: {ifw[0]}\nAdapter name: {ifw[1]}")
            for if_addr in if_addrs[netifaces.AF_INET]:
                print(f"   Address: {if_addr['addr']}")
            print("")

def hex_str(b):
    ret = ""
    for i in b:
        ret += f" {i:02x}"
    return ret[1:]

if __name__ == "__main__":
    # screw it, too many arguments, time for argparse
    parser = argparse.ArgumentParser(description='cat artnet packages')
    parser.add_argument('-i', '--input', help="IP address beginning of art-net network", default="2.*")
    parser.add_argument('-P', '--port', help="art-net port", default=6454, type=int)
    parser.add_argument('-p', '--print', help="Print networks", action='store_true')
    args = parser.parse_args()

    if args.print:
        print_networks()
        exit(0)

    addr_in, bc_in = get_addr(args.input)


    sock_in = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # sock_in.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock_in.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock_in.bind((addr_in, args.port))

    while True:
        data, addr = sock_in.recvfrom(1024)
        opcode = int.from_bytes(data[8:10], "little")
        proto_ver = int.from_bytes(data[10:12], "big")
        sequence = data[12]
        phy = data[13]
        universe = int.from_bytes(data[14:16], "little")
        length = int.from_bytes(data[16:18], "big")
        print(f"o{opcode:x} v{proto_ver} s{sequence} p{phy} u{universe} l{length} || " + hex_str(data[18:]))
        sys.stdout.flush()