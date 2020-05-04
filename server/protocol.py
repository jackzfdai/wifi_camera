#Jack Dai May 2020

from enum import Enum
import struct 

class PROTOCOL_ERRS(Enum):
	PROTOCOL_OK = 0
	PROTOCOL_ERR_CHN_BUSY = 1
	PROTOCOL_ERR_NO_MEM = 3
	PROTOCOL_ERR_NOT_CONNECTED = 4
	PROTOCOL_ERR_INVALID_ARG = 5
	PROTOCOL_ERR_GENERIC = 6

class packet:
	def __init__(self, buffer):
		header = struct.unpack('<BBBBII', buffer[0:12])
		self.frame_id = header[0]
		self.type = header[1]
		self.total_packet_number = header[2]
		self.pkt_sequence = header[3]
		self.transmitter_timestamp = header[4]
		self.payload_len = header[5]
		self.payload = buffer[12:len(buffer)]


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
		print("frame ready!")

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
	def __init__(self, addr, port):
		self.addr = addr 
		self.port = port
		self.frame_list = []
	
	def is_camera(self, addr, port):
		return (addr == self.addr) and (port == self.port)

	def recv_pkt(self, pkt):
		for frame_item in self.frame_list:
			if frame_item.is_part_of_frame(pkt):
				if frame_item.add_packet(pkt):
					return True
		new_frame = frame(pkt) 
		self.frame_list.insert(new_frame.id, new_frame)
		return True

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

		



	
	
	
	


