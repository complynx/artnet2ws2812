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


if __name__ == "__main__":
    # screw it, too many arguments, time for argparse
    parser = argparse.ArgumentParser(description='cat artnet packages')
    parser.add_argument('-i', '--input', help="IP address beginning of art-net network", default="2.*")
    parser.add_argument('-o', '--output', help="IP address beginning of destination network", default="192.*")
    parser.add_argument('-P', '--port', help="art-net port", default=6454, type=int)
    parser.add_argument('--destination-port', help="art-net port", dest='destination_port', default=-1, type=int)
    parser.add_argument('-p', '--print', help="Print networks", action='store_true')
    args = parser.parse_args()

    dest_port = args.destination_port if args.destination_port > 0 else args.port

    if args.print:
        print_networks()
        exit(0)

    addr_in, bc_in = get_addr(args.input)
    addr_out, bc_out = get_addr(args.output)


    sock_in = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # sock_in.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock_in.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock_in.bind((addr_in, args.port))

    sock_out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_out.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock_out.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # sock_out.bind((addr_out, PORT))

    while True:
        data, addr = sock_in.recvfrom(1024)
        sock_out.sendto(data, (bc_out, dest_port))
