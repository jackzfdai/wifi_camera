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
import threading

# -----------  Config  ----------
IP_VERSION = 'IPv4'
PORT = 3333
PORT_CTRL = 3332

CAM0_IP = '192.168.1.79' #wrover
CAM1_IP = '192.168.1.77' #devkitC
CAM2_IP = '192.168.1.1' #filler
CAM3_IP = '192.168.1.2' #filler 

camera0 = protocol.camera(CAM0_IP, PORT)
camera1 = protocol.camera(CAM1_IP, PORT)
camera2 = protocol.camera(CAM2_IP, PORT)
camera3 = protocol.camera(CAM3_IP, PORT)
camera_list = [camera0, camera1, camera2, camera3]
# -------------------------------
class img_display:
    def __init__(self, cameras, total_width, total_height, horiz_frames_max, vert_frames_max):
        self.num_imgs = len(cameras)
        if self.num_imgs > horiz_frames_max*vert_frames_max:
            self.num_imgs = horiz_frames_max*vert_frames_max

        self.cameras = cameras 
        self.horiz_frames = horiz_frames_max
        self.vert_frames = vert_frames_max
        self.img_w = int(total_width/horiz_frames_max)
        self.img_h = int(total_height/vert_frames_max)

        blank = numpy.zeros((self.img_h - 2, self.img_w - 2, int(3)), numpy.uint8)
        blank[:,0:self.img_w] = (220, 220, 220)
        img_cv = cv2.cvtColor(blank, cv2.COLOR_RGB2BGR)
        self.empty_img = cv2.copyMakeBorder(img_cv,1,1,1,1,cv2.BORDER_CONSTANT,value=[0,0,0]) #black

        self.stream_state = [0] * self.num_imgs

        vert_filled = 1
        horiz_filled = 1

        img_horiz = self.empty_img 
        while horiz_filled < self.horiz_frames:
            img_horiz = cv2.hconcat([img_horiz, self.empty_img])
            horiz_filled += 1 

        img_out = img_horiz
        while vert_filled < self.vert_frames:
            img_out = cv2.vconcat([img_out, img_horiz])
            vert_filled += 1 

        self.img = img_out 
            
    def build_new_img(self, jpeg_bytes, camera_num):
        if camera_num > self.num_imgs:
            return
        bitstream = io.BytesIO(bytearray(jpeg_bytes))
        img = Image.open(bitstream)
        cv_img = cv2.cvtColor(numpy.array(img), cv2.COLOR_RGB2BGR)
        resize_img = cv2.resize(cv_img,(int(self.img_w),int(self.img_h)))
        
        row = int(camera_num) / int(self.horiz_frames)
        col = camera_num - row*self.horiz_frames 
        pix_pos_h = int(row * self.img_h)
        pix_pos_w = int(col  * self.img_w)
        self.img[pix_pos_h:(pix_pos_h + self.img_h), pix_pos_w:(pix_pos_w + self.img_w)] = resize_img 
        self.stream_state[camera_num] = 1
        return self.img 

    def clear_old_frames(self):
        index = 0
        while index < self.num_imgs:
            if (not self.cameras[index].is_streaming()) and self.stream_state[index] == 1:
                row = int(index) / int(self.horiz_frames)
                col = index - row*self.horiz_frames
                pix_pos_h = int(row * self.img_h)
                pix_pos_w = int(col  * self.img_w)
                self.img[pix_pos_h:(pix_pos_h + self.img_h), pix_pos_w:(pix_pos_w + self.img_w)] = self.empty_img 
                self.stream_state[index] = 0
            index += 1
        return self.img 

    def display_thread(self):
        while True: 
            camera_index = 0
            while camera_index < self.num_imgs:
                complete_frame = camera_list[camera_index].get_frame()
                if complete_frame:
                    self.build_new_img(complete_frame, camera_index)
                camera_index += 1                    

            img_display = self.clear_old_frames() 

            # new_time = time.time() 
            # frame_period = new_time - prev_time
            # prev_time = new_time
            # mov_avg = period_mov_avg.add_moving_avg(frame_period)

            # print("complete frame received period " + str(frame_period) + " moving avg " + str(mov_avg))
            # if (test == 1):
            # bitstream = io.BytesIO(bytearray(complete_frame))
            # img = Image.open(bitstream)
            # cv_img = cv2.cvtColor(numpy.array(img), cv2.COLOR_RGB2BGR)
            # new_w = cv_img.shape[1]*3
            # new_h = cv_img.shape[0]*3
            # resize_img = cv2.resize(cv_img,(int(new_w),int(new_h)))
            # cv2.imshow('image', resize_img)
            cv2.imshow('image', img_display)

            if cv2.waitKey(1) & 0xFF == ord('q'):
                break        

            time.sleep(0.01)

    def start_display(self):
        self.img_thread = threading.Thread(target = self.display_thread, args = (), daemon = True)
        self.img_thread.start()


        
def find_camera (list, ip_addr, port):
    for camera_item in list:
        if camera_item.is_camera(ip_addr, port):
            return list.index(camera_item)
    return -1

def camera_outbox_thread (cameras, socket, socket_lock):
    while True:
        for camera_item in cameras: 
            data = camera_item.get_outbound_pkt()
            if (data):
                socket_lock.acquire()
                sock.sendto(data.buffer, (camera_item.addr, camera_item.port))
                socket_lock.release() 
        time.sleep(0.05)

def user_input_thread(cameras):
    while True:
        input_cmd = input("Command (ex. stream, stop): ")
        input_num = input("Camera number (from 0): ")

        if input_cmd != "stream" and input_cmd != "stop" or int(input_num) >= len(cameras):
            print("Invalid input")
            continue 

        if input_cmd == "stream":
            cameras[int(input_num)].stream_rqst()
        elif input_cmd == "stop":
            cameras[int(input_num)].stream_stop()

if IP_VERSION == 'IPv4':
    family_addr = socket.AF_INET
elif IP_VERSION == 'IPv6':
    family_addr = socket.AF_INET6
else:
    print('IP_VERSION must be IPv4 or IPv6')
    sys.exit(1)

try:
    sock = socket.socket(family_addr, socket.SOCK_DGRAM)
    sock.setblocking(0)
except socket.error as msg:
    print('Failed to create socket. Error Code : ' + str(msg[0]) + ' Message ' + msg[1])
    sys.exit()

display = img_display(camera_list, 1280, 896, 2, 2) 

sock_lock = threading.Lock()

frame_period = 0
period_mov_avg = diag.moving_avg(10)
prev_time = 0

input_thread = threading.Thread(target = user_input_thread, args = (camera_list,), daemon = True)
input_thread.start() 
outbox_thread = threading.Thread(target = camera_outbox_thread, args = (camera_list, sock, sock_lock,), daemon = True)
outbox_thread.start() 

display.start_display()

while True:
    try:
        # print('Waiting for data...')
        # sock_lock.acquire()
        data, addr = sock.recvfrom(1024)
        # sock_lock.release() 
        if not data:
            continue

        camera_index = find_camera(camera_list, addr[0], addr[1])
        if camera_index == -1:
            print("Unknown data source")
            continue

        data_pkt = protocol.packet(data)
        camera_list[camera_index].recv_pkt(data_pkt)
  

    except socket.error:
        continue
    #     print('Error Code : ' + str(msg[0]) + ' Message ' + msg[1])


sock.close()
cv2.destroyAllWindows()


