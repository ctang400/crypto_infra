import socket
import struct
import array
server_address = '/spare/local/.smb_manager/root/sock/s'
socket_family = socket.AF_UNIX
socket_type = socket.SOCK_SEQPACKET

sock = socket.socket(socket_family, socket_type)
sock.connect(server_address)
channel_name = b'smbcast://test.0\x00'
channel_name_len = len(channel_name)
print(channel_name_len)
msg_len = 8 + channel_name_len
msg = struct.pack('ccHI{}s'.format(channel_name_len), b'\x02', b'W', msg_len, 1024, channel_name)
print(msg)
sock.sendall(msg)
data = sock.recv(1024)
print(type(data))
print(repr(data))
fds = array.array("i")   # Array of ints
msglen = 4
maxfds = 1
msg, ancdata, flags, addr = sock.recvmsg(msglen, socket.CMSG_LEN(maxfds * fds.itemsize))
for cmsg_level, cmsg_type, cmsg_data in ancdata:
  if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            # Append data, ignoring any truncated integers at the end.
    fds.frombytes(cmsg_data[:len(cmsg_data) - (len(cmsg_data) % fds.itemsize)])
print(fds[0])
#print(f"recv data from server '{server_address}': {struct.unpack('cc', data)}")
sock.close()