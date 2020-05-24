#Jack Dai May 2020

import struct 
import threading
import time



class packet:
	def __init__(self, buffer):
		header = struct.unpack('<BBBBIq', buffer[0:16])
		self.frame_id = header[0]
		self.type = header[1]
		self.total_packet_number = header[2]
		self.pkt_sequence = header[3]
		self.payload_len = header[4]
		self.transmitter_timestamp = header[5]
		self.payload = buffer[16:len(buffer)]

class packet_out:
	def __init__(self, frame_id, pkt_type, total_pkt_number, pkt_sequence, transmitter_timestamp, payload_len, payload):
		self.buffer = struct.pack('<BBBBIq', frame_id, pkt_type, total_pkt_number, pkt_sequence, payload_len, transmitter_timestamp)
		self.buffer = self.buffer + struct.pack('<B', payload) 

class frame:
	def __init__(self, pkt):
		self.id = pkt.frame_id
		self.type = pkt.type
		self.total_packet_number = pkt.total_packet_number
		self.transmitter_timestamp = pkt.transmitter_timestamp
		self.payload_list = []
		self.packets_received = 0
		self.frame_complete = False
		self.add_packet(pkt)
		# self.payload_list.insert(pkt.pkt_sequence - 1, pkt.payload) #pkt sequence is indexed at 1 in current protocol design
		# self.packets_received = 1

	def is_part_of_frame(self, pkt):
		if self.id == pkt.frame_id and self.type == pkt.type and self.transmitter_timestamp == pkt.transmitter_timestamp:
			return True
		else:
			return False 

	def add_packet(self, pkt):
		if self.id == pkt.frame_id and self.type == pkt.type and self.transmitter_timestamp == pkt.transmitter_timestamp and self.packets_received != self.total_packet_number:
			self.payload_list.insert(pkt.pkt_sequence - 1, pkt.payload) #pkt sequence is indexed at 1 in current protocol design
			self.packets_received += 1
			if self.packets_received == self.total_packet_number:
				self.signal_frame_ready()
			return True
		else:
			return False

	def signal_frame_ready(self):
		self.frame_complete = True
		# print("frame ready!")

	def get_frame_data(self):
		if self.packets_received == self.total_packet_number: #TODO: can also use the frame_complete flag
			combined_list = []
			for pkt_payload in self.payload_list:
				for val in pkt_payload:
					combined_list.append(val)
			return combined_list
		else:
			return -1


class camera:
	STATE_IDLE = 0
	STATE_STREAMING = 1

	CONN_TIMEOUT_INTERVAL = 5
	KEEPALIVE_INTERVAL = 1

	PROTOCOL_CTRL_PKT = 0xF
	PROTOCOL_DATA_PKT = 0xF + 1
	PROTOCOL_ERR_PKT = 0xF + 2

	PROTOCOL_STREAM_RQST = 0xF
	PROTOCOL_STREAM_STOP = 0xF + 1
	PROTOCOL_STREAM_KEEPALIVE = 0xF + 2


	def __init__(self, addr, port):
		self.addr = addr 
		self.port = port
		self.frame_list = []
		self.out_frame_id = 0
		self.out_pkt_list = [] 
		self.state = self.STATE_IDLE
		self.pkt_recved = 0 
		self.keepalive = threading.Thread(target = self.keepalive_thread, args = (), daemon = True)
		self.conn_timeout = threading.Thread(target = self.conn_timeout_thread, args = (), daemon = True)
		self.keepalive.start()
		self.conn_timeout.start()

	def keepalive_thread(self):
		while True:
			if self.state == self.STATE_STREAMING:
				pkt = packet_out(self.out_frame_id, self.PROTOCOL_CTRL_PKT, 1, 1, 0, 1, self.PROTOCOL_STREAM_KEEPALIVE)
				self.out_pkt_list.append(pkt)
			time.sleep(self.KEEPALIVE_INTERVAL)

	def conn_timeout_thread(self):
		while True:
			if self.state == self.STATE_STREAMING:
				if self.pkt_recved == 0:
					self.state = self.STATE_IDLE
					print("conn timeout")
				self.pkt_recved = 0 
			time.sleep(self.CONN_TIMEOUT_INTERVAL)
	
	def is_camera(self, addr, port):
		return (addr == self.addr) and (port == self.port)

	def recv_pkt(self, pkt):
		if (self.state == self.STATE_STREAMING):
			self.pkt_recved = 1 

			for frame_item in self.frame_list:
				if frame_item.is_part_of_frame(pkt):
					if frame_item.add_packet(pkt):
						return True
			new_frame = frame(pkt) 
			self.frame_list.insert(new_frame.id, new_frame)
			return True
		else: 
			return False 

	def get_frame(self):
		for frame_item in self.frame_list:
			if frame_item.frame_complete == True:
				index = self.frame_list.index(frame_item)
				i = 0
				while (i < index):
					print("index " + str(i) + "index max " + str(index) + "len " + str(len(self.frame_list)))
					self.frame_list.pop(0) #always pop the front element of the list
					i += 1
					# print("Popped incomplete frame")
				# print("Size of list " + str(len(self.frame_list)) + " popping complete frame index " + str(index))
				completed_frame = self.frame_list.pop(index - i) #effectively 0, as item originally at index will have been moved to front of list due to popping in while loop 
				frame_payload = completed_frame.get_frame_data() 
				if frame_payload != -1:
					return frame_payload
				else:
					break 
		return False 

	def stream_rqst(self):
		if self.state == self.STATE_IDLE:
			self.state = self.STATE_STREAMING
			pkt = packet_out(self.out_frame_id, self.PROTOCOL_CTRL_PKT, 1, 1, 0, 1, self.PROTOCOL_STREAM_RQST)
			self.out_pkt_list.append(pkt)
			self.pkt_recved = 0 

		else:
			print("Invalid state")
		#send stream rqst to camera 

	def stream_stop(self):
		if self.state == self.STATE_STREAMING:
			self.state = self.STATE_IDLE
			pkt = packet_out(self.out_frame_id, self.PROTOCOL_CTRL_PKT, 1, 1, 0, 1, self.PROTOCOL_STREAM_STOP)
			self.out_pkt_list.append(pkt)
			self.pkt_recved = 0 

	def get_outbound_pkt(self):
		if (len(self.out_pkt_list) > 0): 
			return self.out_pkt_list.pop(0)
		else:
			return

	def is_streaming(self):
		return (self.state == self.STATE_STREAMING)


		



	
