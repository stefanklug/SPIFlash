#ifndef _FLASHWEARLEVELER_H_
#define _FLASHWEARLEVELER_H_

#include <inttypes.h>

template<typename Flash>
class FlashWearLeveler {
public:
	FlashWearLeveler(Flash& flash, uint16_t noOf4kBlocks);
	~FlashWearLeveler();
	bool initialize();
	uint8_t readByte(long addr);
	void readBytes(long addr, void* buf, long len);
	void writeByte(long addr, uint8_t byt);
	void writeBytes(long addr, const void* buf, int len);

	bool flushNeeded();
	void flush();

	long virtual2physicalAddr(long addr);
	long physical2virtualAddr(long addr);
	long getSize();
protected:
	uint16_t readBlockHeader(uint16_t physicalBlockId);
	uint16_t getActiveBlockHeader();
	void activateVirtualBlock(uint16_t virtualBlockHeader);

	Flash& flash;
	uint16_t blockCount;
	uint8_t activeBlock[4096];
	bool activeBlockDirty;
	//maps virtual block ids to real blocks (it contains block headers, encoding the physical block, the deleted bit normally = 1)
	//for unused virtual blocks, it still contains a header pointing to a physical block, but with the deleted bit = 0
	uint16_t* blockMap;
	//array of the physical Block Headers needed for fast free block lookup
	//it is the inverse of block Map, so for an empty physicalBlock it contains a virtual block id, and the deleted bit = 0
	uint16_t* blockHeaderCache;
};

#endif
