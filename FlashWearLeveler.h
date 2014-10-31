#ifndef _FLASHWEARLEVELER_H_
#define _FLASHWEARLEVELER_H_

#include <inttypes.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

typedef struct addr_info_ addr_info;

class FlashWearLevelerBase {
public:
	//the pointers are passed in, to be able to statically allocate them inside the templated FlashWearLeveler
	FlashWearLevelerBase(uint16_t noOf4kBlocks, uint16_t* blockMapMem, uint16_t* blockHeaderCacheMem);
	virtual ~FlashWearLevelerBase();
	bool initialize();
	bool format();
	uint8_t readByte(long addr);
	int readBytes(long addr, void* buf, long len);
	int writeByte(long addr, uint8_t byt);
	int writeBytes(long addr, const void* buf, int len);

	bool flushNeeded();
	void flush();

	long virtual2physicalAddr(long addr);
	long physical2virtualAddr(long addr);
	long getSize();

	void printCaches();
protected:
	uint16_t readBlockHeader(uint16_t physicalBlockId);
	uint16_t getActiveBlockHeader();
	void activateVirtualBlock(uint16_t virtualBlockHeader);
	int readBytesFromVBlock(const addr_info& virtualStartInfo, void* buf, long len);

	virtual uint8_t flashReadByte(long addr) = 0;
	virtual int flashReadBytes(long addr, void* buf, long len)=0;
	virtual int flashWriteByte(long addr, uint8_t byt)=0;
	virtual int flashWriteBytes(long addr, const void* buf, int len)=0;
	virtual int flashChipErase()=0;
	virtual int flashBlockErase4K(long address)=0;

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

template<typename Flash, int noOf4kBlocks>
class FlashWearLeveler: public FlashWearLevelerBase {
public:
	 FlashWearLeveler(Flash& _flash):FlashWearLevelerBase(noOf4kBlocks, bM, bMC), flash(_flash) {}
protected:
	virtual uint8_t flashReadByte(long addr) { return flash.readByte(addr); }
	virtual int flashReadBytes(long addr, void* buf, long len) { flash.readBytes(addr, buf, len); return 0; }
	virtual int flashWriteByte(long addr, uint8_t byt) { flash.writeByte(addr, byt); return 0; }
	virtual int flashWriteBytes(long addr, const void* buf, int len){ flash.writeBytes(addr, buf, len); return 0; }
	virtual int flashChipErase() {
		Serial.println("Erase");
		flash.chipErase();
		while(flash.busy()) {
			Serial.println("Wait for erase");
		}
		return 0;
	}
	virtual int flashBlockErase4K(long address) {
		flash.blockErase4K(address); return 0;
		while(flash.busy()) {
			Serial.println("Wait for block erase");
		}
		return 0;
	}

	Flash& flash;
	uint16_t bM[noOf4kBlocks];
	uint16_t bMC[noOf4kBlocks];
};

#endif
