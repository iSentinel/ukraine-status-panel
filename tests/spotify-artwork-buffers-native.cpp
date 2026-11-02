#include "artwork_buffers.h"
#include <cassert>
#include <cstdio>
using namespace usp_spotify_artwork_buffers;
int main() {
  {
    Buffers buffers;
    assert(!buffers.compressed() && !buffers.workspace() && !buffers.frame(0));
    fixture::fail=true;assert(!buffers.ensure());assert(fixture::allocations==1);
    fixture::fail=false;fixture::external=false;
    assert(!buffers.ensure() && fixture::frees==1 && !buffers.frame(1));
    fixture::external=true;assert(buffers.ensure());
    auto *base=buffers.compressed();assert(base && buffers.workspace()==base+COMPRESSED_BYTES);
    assert(reinterpret_cast<uint8_t *>(buffers.frame(0))==base+COMPRESSED_BYTES+WORKSPACE_BYTES);
    assert(reinterpret_cast<uint8_t *>(buffers.frame(1))==base+TOTAL_BYTES-FRAME_BYTES);
    assert(!buffers.frame(2));
    assert(buffers.ensure() && buffers.compressed()==base && fixture::allocations==3);
    base[COMPRESSED_BYTES-1]=0xA5;
    static_cast<uint8_t *>(buffers.workspace())[WORKSPACE_BYTES-1]=0x5A;
    for(unsigned i=0;i<2;++i)for(size_t p=0;p<FRAME_PIXELS;++p)buffers.frame(i)[p]=uint16_t(i+1);
    assert(base[COMPRESSED_BYTES-1]==0xA5);
    assert(static_cast<uint8_t *>(buffers.workspace())[WORKSPACE_BYTES-1]==0x5A);
    assert(buffers.frame(0)[FRAME_PIXELS-1]==1 && buffers.frame(1)[0]==2);
  }
  assert(fixture::frees==2 && !fixture::base);
  puts("ARTWORK_PSRAM_POOL_PREFLIGHT=PASS allocation_failure wrong_region_retry stable_buffers no_fallback host_only");
}
