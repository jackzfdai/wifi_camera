# This example code is in the Public Domain (or CC0 licensed, at your option.)

# Unless required by applicable law or agreed to in writing, this
# software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied.

# -*- coding: utf-8 -*-

import socket
import sys
import cv2
import protocol
import io 
from PIL import Image
import numpy
import time
import diag

# -----------  Config  ----------
IP_VERSION = 'IPv4'
PORT = 3333
# -------------------------------

def find_camera (list, ip_addr, port):
    for camera_item in list:
        if camera_item.is_camera(ip_addr, port):
            return list.index(camera_item)
    return -1

if IP_VERSION == 'IPv4':
    family_addr = socket.AF_INET
elif IP_VERSION == 'IPv6':
    family_addr = socket.AF_INET6
else:
    print('IP_VERSION must be IPv4 or IPv6')
    sys.exit(1)


try:
    sock = socket.socket(family_addr, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
except socket.error as msg:
    print('Failed to create socket. Error Code : ' + str(msg[0]) + ' Message ' + msg[1])
    sys.exit()

try:
    sock.bind(('', PORT))
except socket.error as msg:
    print('Bind failed. Error: ' + str(msg[0]) + ': ' + msg[1])
    sys.exit()

camera_list = []
frame_period = 0
period_mov_avg = diag.moving_avg(10)
prev_time = 0
while True:
    try:
        print('Waiting for data...')
        data, addr = sock.recvfrom(1024)
        if not data:
            break

        # data_int = list(data)
        # print(data_int)

        camera_index = find_camera(camera_list, addr[0], addr[1])
        if camera_index == -1:
            cam_new = protocol.camera(addr[0], addr[1])
            camera_list.append(cam_new)
        
        data_pkt = protocol.packet(data)
        camera_list[camera_index].recv_pkt(data_pkt)

        complete_frame = camera_list[camera_index].get_frame()
        if not complete_frame:
            # print("frame not ready")
            continue

        new_time = time.time() 
        frame_period = new_time - prev_time
        prev_time = new_time
        mov_avg = period_mov_avg.add_moving_avg(frame_period)
        print("complete frame received period " + str(frame_period) + " moving avg " + str(mov_avg))
        # if (test == 1):
        bitstream = io.BytesIO(bytearray(complete_frame))
        img = Image.open(bitstream)
        cv_img = cv2.cvtColor(numpy.array(img), cv2.COLOR_RGB2BGR)
        new_w = cv_img.shape[1]*3
        new_h = cv_img.shape[0]*3
        resize_img = cv2.resize(cv_img,(int(new_w),int(new_h)))
        cv2.imshow('image', resize_img)
        
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break        


    except socket.error as msg:
        print('Error Code : ' + str(msg[0]) + ' Message ' + msg[1])


sock.close()
cv2.destroyAllWindows()


