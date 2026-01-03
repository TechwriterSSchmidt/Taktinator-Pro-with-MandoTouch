import os
import struct

def strip_metadata(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    if data[:4] != b'RIFF':
        print(f"Skipping {filepath}: Not a RIFF file")
        return

    file_size = struct.unpack('<I', data[4:8])[0]
    if data[8:12] != b'WAVE':
        print(f"Skipping {filepath}: Not a WAVE file")
        return

    new_data = bytearray(data[:12])
    ptr = 12
    
    while ptr < len(data):
        if ptr + 8 > len(data):
            break
            
        chunk_id = data[ptr:ptr+4]
        chunk_size = struct.unpack('<I', data[ptr+4:ptr+8])[0]
        
        # Check if it's a LIST chunk containing INFO
        if chunk_id == b'LIST':
            list_type = data[ptr+8:ptr+12]
            if list_type == b'INFO':
                print(f"Removing metadata from {filepath}")
                ptr += 8 + chunk_size
                if chunk_size % 2 == 1: ptr += 1 # Padding
                continue
        
        # Copy other chunks (fmt, data, etc.)
        new_data.extend(data[ptr:ptr+8+chunk_size])
        if chunk_size % 2 == 1:
            new_data.append(0) # Padding
            ptr += 1
            
        ptr += 8 + chunk_size

    # Update RIFF size
    new_size = len(new_data) - 8
    new_data[4:8] = struct.pack('<I', new_size)

    with open(filepath, 'wb') as f:
        f.write(new_data)

data_dir = os.path.join(os.path.dirname(__file__), '../data')
for filename in os.listdir(data_dir):
    if filename.lower().endswith('.wav'):
        strip_metadata(os.path.join(data_dir, filename))
