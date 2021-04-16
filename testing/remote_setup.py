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
import random
import fnmatch

import winreg as wr

def hsv2rgb(h,s,v):
    return tuple(round(i * 255) for i in colorsys.hsv_to_rgb(h/360.,s/100.,v/100.))

def send(msg, port, baddr, saddr):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # t = threading.Thread(target=recv, args=(sock,))
    # t.daemon = True
    # t.start()
    print(f"sending to {saddr}:{port}")

    sock.sendto(msg, (saddr, port))


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

def str2col(s):
    if s[0] == "#":
        s = s[1:]
    col_n = int(s, 16)
    col_b = col_n.to_bytes(4, byteorder='big')
    return col_b[1:]


if __name__ == "__main__":
    # screw it, too many arguments, time for argparse
    parser = argparse.ArgumentParser(description='Sends settings to ESP Art-Net')
    parser.add_argument('-d', '--delay', help="repeat message with this delay, if none, don't repeat", type=float, default=-1)
    parser.add_argument('-N', '--network', help="IP address beginning of destination network", default="192.*")
    parser.add_argument('-A', '--address', help="IP address to send", default="")
    parser.add_argument('-P', '--port', help="art-net port", default=6454, type=int)

    subparsers = parser.add_subparsers(title='setting type', help="type of the setting to send", dest='type')

    ap_parser = subparsers.add_parser('ap', aliases=['access-point'], help='onboard access point settings')
    ap_parser.add_argument('-a', '--ap_always', help="hold AP enabled", action='store_true')
    ap_parser.add_argument('ssid', help="AP SSID template\n"+\
        "All the hashes (#) in the string will be filled with device ID in hex representation.\n"+\
        "(%0Nx where N is number of hashes in the string)\n"+\
        "Device ID will be filled in right to left mode, truncated or padded with zeros.\n"+\
        "Hashes don't need to be sequential, they can appear anywhere in the string.")
    ap_parser.add_argument('password', help="AP password", nargs="?", default="")

    sta_parser = subparsers.add_parser('sta', aliases=['stations'], help='sends new list of stations and passwords')
    sta_parser.add_argument('stations', help="station list, in form of: station1 password1[ station2 password2[ ...]]\n"+\
        "if station is open, provide empty string as a password, \"\"", nargs="+")

    dmx_parser = subparsers.add_parser('dmx', help='sets universe and shift')
    dmx_parser.add_argument('universe', help="Art-Net universe number", type=int)
    dmx_parser.add_argument('shift', help="DMX address of the device (where the DMX bits start for this device)", type=int)

    args = parser.parse_args()

    if args.address == "":
        addr_out, bc_out = get_addr(args.network)
    else:
        addr_out = args.address
        bc_out = args.address

    buf_pl = b""
    if args.type == "sta":
        buf_pl += b"\x23\xf8"
        if len(args.stations) < 1 or len(args.stations)%2 != 0:
            parser.error("sta stations have to be chain of ssids and passwords:\n"+\
                ' ... sta "ssid1" "password1" "ssid2" "" "ssid3" "password3"\n'+\
                "in this case, sta ssid2 will be without password")
        for st in args.stations:
            buf_pl += st.encode('utf-8') + b"\x00"
        buf_pl += b"\x00\x00\x00" # end of sta settings marker
    elif args.type == "ap":
        buf_pl += b"\x24\xf8"
        buf_pl += b"\x01" if args.ap_always else b"\x00"
        buf_pl += args.ssid.encode('utf-8') + b"\x00"
        if 0 < len(args.password) < 8:
            parser.error("AP password must be at least 8 characters long")
        if len(args.password) >= 8:
            buf_pl += args.password.encode('utf-8')
        buf_pl += b"\x00"
    else: # dmx
        buf_pl += b"\x25\xf8"
        buf_pl += args.universe.to_bytes(2, byteorder='little')
        buf_pl += args.shift.to_bytes(2, byteorder='little')
    #       |marker
    buf = b"Art-Net\x00" + buf_pl

    i=0
    while True:
        i+=1
        send(buf, args.port, addr_out, bc_out)
        if args.delay > 0:
            time.sleep(args.delay)
        else:
            break

