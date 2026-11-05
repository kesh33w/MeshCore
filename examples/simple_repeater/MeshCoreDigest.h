#pragma once
#include "PrivateRfCapture.h"
#include <SHA256.h>
namespace private_remote {
inline bool link_digest(const uint8_t* raw,size_t len,uint8_t digest[32]) {
 PacketView v{}; if(!digest || !inspect(raw,len,v)) return false;
 SHA256 sha; sha.update(&v.type,1);
 // Private v1 follows host Core TRACE uint16 little-endian hash input.
 // Official MeshCore native dedup uses uint8_t instead; do not change it.
 if(v.type==9) { const uint8_t le[2]={v.encoded_path,0}; sha.update(le,2); }
 sha.update(raw+v.payload_offset,len-v.payload_offset); sha.finalize(digest,32); return true;
}
}
