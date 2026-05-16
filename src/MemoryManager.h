#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <vector>
#include <string>

// NOTE: do not bring all of std:: into the global namespace here.
// `using namespace std;` previously caused `std::byte` to collide with
// the `byte` typedef in <rpcndr.h> on Windows (MinGW GCC 16, C++17).
// Downstream files historically relied on bare `string` / `vector` etc.
// being visible via this header, so import only those names explicitly.
using std::vector;
using std::string;

struct FreeMemoryBlock
{
  uint32 startAddress;
  uint32 endAddress;
  uint32 numBytes;
};

struct AllocatedMemoryBlock
{
  uint32 startAddress;
  uint32 numBytes;
};

class MemoryManager final
{
public:
  MemoryManager() {}
  ~MemoryManager() {}
  void AddAllocatedBlock(const uint32 startAddress, const uint32 numBytes);
  void Add(const uint32 startAddress, const uint32 endAddress, uint32 index = 0);
  uint32 Allocate(uint32 requestedBytes, uint32 requestedAlignment);
  void Free(const uint32 address);

  inline void Reset()
  {
    freeBlockVector.clear();
    allocatedBlockVector.clear();
  }

private:
  uint32 AlignAddress(const uint32 address, const uint32 alignment)
  {
    return (address + alignment - 1) & (~(alignment - 1));
  }
  bool TestForInvalidPowerOfTwo(const uint32 requestedAlignment)
  {
    return (requestedAlignment & (requestedAlignment - 1)) != 0;
  }

  vector<FreeMemoryBlock> freeBlockVector;
  vector<AllocatedMemoryBlock> allocatedBlockVector;
};

#endif
