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
    parser = argparse.ArgumentParser(description='cat artnet packages')
    parser.add_argument('-s', '--shift', help="art net shift", default=0, type=int)
    parser.add_argument('-n', '--number', help="diodes number", default=34, type=int)
    parser.add_argument('-u', '--universe', help="universe", default=1, type=int)
    parser.add_argument('-d', '--delay', help="delay", type=float, default=-1)
    parser.add_argument('-N', '--network', help="IP address beginning of destination network", default="192.*")
    parser.add_argument('-P', '--port', help="art-net port", default=6454, type=int)
    parser.add_argument('-t', '--type', help="test type",
     choices=['default', 'full_range', "rainbow", "chain", "chain_reversed"], default="default")
    parser.add_argument('--rainbow_delay', help="delay in the rainbow params, in ms, will be rounded to ticks",
     type=int, default=0x50)
    parser.add_argument('--rainbow_time_step', help="increment in hue in each time step",
     type=int, default=1)
    parser.add_argument('--rainbow_length_step', help="increment in hue in each length step",
     type=int, default=1)
    parser.add_argument('--rainbow_start_color', help="color of 1st pixel at the T=0", default="#880000")
    parser.add_argument('--rainbow_tint_color', help="color of tint", default="#000000")
    parser.add_argument('--rainbow_tint_value', help="tint weight col_final = col (255-tint_value) + tint_col * tint_value",
     default=0, type=int)
    args = parser.parse_args()

    addr_out, bc_out = get_addr(args.network)

    delay = args.delay
    if delay <= 0:
        if args.type == "rainbow":
            delay = 5
        elif args.type.startswith("chain"):
            delay = .4
        else:
            delay = 1/20.

    rainbow_id = random.randint(1,255)

    i=0
    led_len = args.number
    inc = max(1, 360/led_len)
    while True:
        i+=1
        buf_pl = b"\x00" * (args.shift)
        if args.type == "rainbow":
            #           |RBW|ID |Delay  |T step |L step |start color|tint color |tint value
            buf_pl += b"\x03"+\
                        rainbow_id.to_bytes(1, byteorder='little')+\
                        args.rainbow_delay.to_bytes(2, byteorder='big')+\
                        args.rainbow_time_step.to_bytes(2, byteorder='big')+\
                        args.rainbow_length_step.to_bytes(2, byteorder='big')+\
                        str2col(args.rainbow_start_color)+\
                        str2col(args.rainbow_tint_color)+\
                        args.rainbow_tint_value.to_bytes(1, byteorder='little')
        elif args.type.startswith("chain"):
            buf_pl += b"\x02" if args.type == "chain_reversed" else b"\x01"
            buf_pl += bytes(hsv2rgb(random.randint(0, 359),100,50))
        else:
            buf_pl += b"\x00"  # Straight DMX
            for l in range(0, led_len):
                buf_pl += bytes(hsv2rgb(
                    (i + (l*inc))%360,
                    100 if args.type == "default" or i%240 > 120 else 0,
                    50 if args.type == "default" else abs(i%120 - 60)/0.6))
        buf_pl += b"\x00" * (512 - len(buf_pl))
        #       |marker    |opcode |proto  |seq|phy|univ   |len    |payload (512 bytes)
        buf = b"Art-Net\x00\x00\x50\x00\x0e\x00\x00"+args.universe.to_bytes(2, byteorder='little')+b"\x02\x00" + \
        buf_pl
        send(buf, args.port, addr_out, bc_out)
        time.sleep(delay)

# buf = b'\xa4l\xf1[\xed\xde\x01\x02\x00\x00\x00\x00\x00\x00' # \x08\xf1\x88l
# buf += socket.htonl(zlib.crc32(buf, 0xCA7ADDED)).to_bytes(4,'little')
# print(buf)
