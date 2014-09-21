#include "FlashWearLeveler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <Arduino.h>

#define BLOCK_NOT_DELETED_BIT (1<<15)

//masks the last 14bits. the first two bits are reserved as flags
#define BLOCK_ID(v) ((v) & 0x3fff)

//block is deleted, if bit 15 is 0. This
#define BLOCK_DELETED(v) ( !((v) & (1<<15)) )
//block is free, if it is either 0xffff or the deleted bit is 0
#define BLOCK_IS_FREE(v) ((v) == 0xffff || !( (v) & (1<<15) ) )

#define VIRTUAL_BLOCK_SIZE 4094
#define PHYSICAL_BLOCK_SIZE 4096

//represents an address as block index and offset into the block
struct addr_info_ {
	uint16_t block;
	uint16_t offset;

	inline bool operator == (const addr_info_ &b) const {
		return block == b.block && offset == b.offset;
	}

	inline bool operator != (const addr_info_ &b) const {
		return !(*this == b);
	}
};

const uint16_t ErasedHeader = 0xffff;

#define FWL_ERR(...) Serial.printf(__VA_ARGS__); Serial.println("");
//#define FWL_ERR(...) printf(__VA_ARGS__); printf("\n");

static addr_info SplitVirtualAddress(long addr) {
	addr_info res;
	res.block = addr/VIRTUAL_BLOCK_SIZE;
	res.offset = addr - res.block*VIRTUAL_BLOCK_SIZE;
	return res;
}

static addr_info SplitPhysicalAddress(long addr) {
	//TODO here ((long)res) = addr; should be the same, if PHYSICAL_BLOCK_SIZE == 4096
	addr_info res;
	res.block = addr/PHYSICAL_BLOCK_SIZE;
	res.offset = addr - res.block*PHYSICAL_BLOCK_SIZE;
	if(res.offset < 2) {
		FWL_ERR("Can't split physical address %08lx. It is not in the mapped area", addr);
	}
	//subtract the header
	res.offset -= 2;
	return res;
}

static long CombineVirtualAddress(addr_info info) {
	return (long)info.block * VIRTUAL_BLOCK_SIZE + info.offset;
}

static long CombinePhysicalAddress(addr_info info) {
	//add the header
	return (long)info.block * PHYSICAL_BLOCK_SIZE + info.offset + 2;
}



FlashWearLevelerBase::FlashWearLevelerBase(uint16_t noOf4kBlocks): blockCount(noOf4kBlocks) {
	blockMap = (uint16_t*)malloc(noOf4kBlocks * sizeof(uint16_t));
	if(!blockMap) FWL_ERR("failed to allocate map");
	assert(blockMap != 0);
	blockHeaderCache = (uint16_t*)malloc(noOf4kBlocks * sizeof(uint16_t));
	if(!blockHeaderCache) FWL_ERR("failed to allocate map");
	assert(blockHeaderCache != 0);
}


FlashWearLevelerBase::~FlashWearLevelerBase() {
	free(blockMap);
	blockMap = 0;
	free(blockHeaderCache);
	blockHeaderCache = 0;
}


bool FlashWearLevelerBase::initialize() {
	//if(!flashinitialize()) return false;
	activeBlockDirty = false;

	//clean the current block cache
	memset(activeBlock, 0xFF, PHYSICAL_BLOCK_SIZE);

	//initialize the map with ff (unused)
	memset(blockMap, 0xFF, blockCount * sizeof(uint16_t));

	//iterate through the physical blocks to fill the block map
	int i;
	for(i=0; i<blockCount; i++) {
		uint16_t virtualBlockId = readBlockHeader(i);
		blockHeaderCache[i] = virtualBlockId;
		if(virtualBlockId != 0xffff) {
			if(BLOCK_ID(virtualBlockId) > blockCount) {
				FWL_ERR("Block id > blockCount. You should format the flash");
				return false;
			}

			if(BLOCK_DELETED(virtualBlockId)) {
				FWL_ERR("Found deleted block. Deleting...");
				//flashblockErase4K(i*4096);
				continue;
			}

			//mark as NOT deleted by setting the deleted bit
			blockMap[BLOCK_ID(virtualBlockId)] = (i | BLOCK_NOT_DELETED_BIT);
		}
	}

	//now iterate over the virtual blocks to fill the holes
	int lastFreePhysicalBlock = 0;
	for(i=0; i<blockCount; i++) {
		//if it is marked as free, lets assign a physical block
		if(blockMap[i] == 0xffff) {

			int y;
			//find a free physical block and assign it to this virtual one
			for( ; lastFreePhysicalBlock<blockCount; lastFreePhysicalBlock++) {
				//uint16_t header = readBlockHeader(lastFreePhysicalBlock);
				uint16_t header = blockHeaderCache[lastFreePhysicalBlock];
				if(BLOCK_IS_FREE(header)) {
					//sucessfully filled hole
					//the deleted bit is automatically zero, which is correct
					blockMap[i] = lastFreePhysicalBlock;
					//now we know the virtualBlock, this block got assigned to, so we can update the value
					blockHeaderCache[lastFreePhysicalBlock] = i;
					break;
				}
			}

			if(lastFreePhysicalBlock == blockCount) {
				//we should never end up here
				FWL_ERR("Didn't find enough free blocks");
				return false;
			}

			lastFreePhysicalBlock++;
		}
	}
	printCaches();
	return true;
}

bool FlashWearLevelerBase::format() {
	flashChipErase();
	return initialize();
}

void FlashWearLevelerBase::printCaches() {
	//return;
#if 1
	int i;
	Serial.printf("V->P ");
	for(i=0; i<blockCount; i++) {
		Serial.printf("%s %03i ", (BLOCK_IS_FREE(blockMap[i]) ? "f" : "n"), BLOCK_ID(blockMap[i]));
	}
	Serial.printf("\n");
	Serial.printf("P->V ");
	for(i=0; i<blockCount; i++) {
		Serial.printf("%s %03i ", (BLOCK_IS_FREE(blockHeaderCache[i]) ? "f" : "n"), BLOCK_ID(blockHeaderCache[i]));
	}
	Serial.printf("\n\n");
#endif
}


uint8_t FlashWearLevelerBase::readByte(long addr) {
	uint16_t h = getActiveBlockHeader();
	if(!BLOCK_IS_FREE(h)) {
		//active block is valid, see, if we need to read from there

		//check if the addr lies in the activeBlock
		addr_info i = SplitVirtualAddress(addr);
		if(i.block == BLOCK_ID(h)) {
			//add the header
			return activeBlock[i.offset+2];
		}
	}

	//just forward
	return flashReadByte(addr);
}


int FlashWearLevelerBase::readBytes(long addr, void* buf, long len) {
	//iterate over the blocks
	addr_info start = SplitVirtualAddress(addr);
	addr_info end = SplitVirtualAddress(addr + len);
	int status = 0;

	int pos = 0;
	while(start != end) {
		long physicalAddress = CombinePhysicalAddress(start);
		int len;
		if(end.block > start.block) {
			//copy the rest
			len = VIRTUAL_BLOCK_SIZE - start.offset;
			status = readBytesFromVBlock(start, buf, len);
			if(status != 0) return status;
			start.block++;
			start.offset=0;
		} else {
			len = end.offset - start.offset;
			status = readBytesFromVBlock(start, buf, len);
			if(status != 0) return status;
			start.offset = end.offset;
		}
		buf = (uint8_t*)buf + len;
	}
	return status;
}


int FlashWearLevelerBase::readBytesFromVBlock(const addr_info& virtualStartInfo, void* buf, long len) {
	assert(virtualStartInfo.offset + len < VIRTUAL_BLOCK_SIZE);
	int status = 0;
	uint16_t h = getActiveBlockHeader();
	//see if we need to copy from the active Block
	if(!BLOCK_IS_FREE(h) && BLOCK_ID(h) == virtualStartInfo.block) {
		memcpy(buf, activeBlock + virtualStartInfo.offset + 2, len);
	} else {
		addr_info physicalInfo;
		physicalInfo.block = BLOCK_ID(blockMap[virtualStartInfo.block]);
		physicalInfo.offset = virtualStartInfo.offset;
		status = flashReadBytes(CombinePhysicalAddress(physicalInfo), buf, len);
	}
	return status;
}


int FlashWearLevelerBase::writeByte(long addr, uint8_t byt) {
	addr_info virtualInfo = SplitVirtualAddress(addr);
	activateVirtualBlock(virtualInfo.block);

	activeBlock[virtualInfo.offset + 2] = byt;
	activeBlockDirty = true;
	return 0;
}



int FlashWearLevelerBase::writeBytes(long addr, const void* buf, int len) {
	addr_info start = SplitVirtualAddress(addr);
	addr_info end = SplitVirtualAddress(addr + len);

	while(start != end) {
		activateVirtualBlock(start.block);
		if(end.block > start.block) {
			//copy the rest
			len = VIRTUAL_BLOCK_SIZE - start.offset;
			memcpy(activeBlock + start.offset + 2, buf, len);
			start.block++;
			start.offset=0;
		} else {
			len = end.offset - start.offset;
			memcpy(activeBlock + start.offset + 2, buf, len);
			start.offset = end.offset;
		}
		activeBlockDirty = true;
		buf = (uint8_t*)buf + len;
	}

	return 0;
}


void FlashWearLevelerBase::activateVirtualBlock(uint16_t virtualBlockHeader) {
	uint16_t header = getActiveBlockHeader();
	if(BLOCK_ID(virtualBlockHeader) != BLOCK_ID(header)) {
		flush();
		uint16_t physicalBlockHeader = blockMap[BLOCK_ID(virtualBlockHeader)];
		flashReadBytes(BLOCK_ID(physicalBlockHeader)*PHYSICAL_BLOCK_SIZE, activeBlock, 4096);
		//it might be that we load a erased flash page, were the header would be 0xffff. Lets correct that and mark the block as unfree
		header = blockHeaderCache[BLOCK_ID(physicalBlockHeader)] | BLOCK_NOT_DELETED_BIT;
		((uint16_t*)activeBlock)[0] = header;
		blockHeaderCache[BLOCK_ID(physicalBlockHeader)] = header;
		assert(BLOCK_ID(virtualBlockHeader) == BLOCK_ID(getActiveBlockHeader()));
	}
}


uint16_t FlashWearLevelerBase::readBlockHeader(uint16_t physicalBlockId) {
	uint16_t res;
	flashReadBytes(physicalBlockId*PHYSICAL_BLOCK_SIZE, &res, sizeof(uint16_t));
	return res;
}


uint16_t FlashWearLevelerBase::getActiveBlockHeader() {
	return ((uint16_t*)activeBlock)[0];
}


bool FlashWearLevelerBase::flushNeeded() {
	return activeBlockDirty;
}


void FlashWearLevelerBase::flush() {
	if(!activeBlockDirty) return;
	//header contains the virtual block id
	uint16_t header = getActiveBlockHeader();

	//physicalBlock contains a block header pointing to the current physical Block in use
	uint16_t currentPhysicalBlock = blockMap[BLOCK_ID(header)];

	uint16_t nextPhysicalBlock;

	//if the block is free (first write to this virtual block) use it directly
	if(BLOCK_IS_FREE(currentPhysicalBlock)) {
		nextPhysicalBlock = currentPhysicalBlock;
	} else {
		//search a free physical block, starting from the current physical block
		nextPhysicalBlock = ErasedHeader;
		int i;
		for(i=BLOCK_ID(currentPhysicalBlock); i<blockCount; i++) {
			if(BLOCK_IS_FREE(blockHeaderCache[i])) {
				nextPhysicalBlock = i;
				break;
			}
		}

		//no block found, wrap around
		if(nextPhysicalBlock == ErasedHeader) {
			for(i=0; i<BLOCK_ID(currentPhysicalBlock); i++) {
				if(BLOCK_IS_FREE(blockHeaderCache[i])) {
					nextPhysicalBlock = i;
					break;
				}
			}
		}

		if(nextPhysicalBlock == ErasedHeader) {
			FWL_ERR("Didn't find free block to write to");
			return;
		}
	}

	//usedVirtualBlock will point to a free virtual block
	uint16_t usedVirtualBlock = blockHeaderCache[BLOCK_ID(nextPhysicalBlock)];

	//write the new physical block
	//construct the physical address to write
	long addr;
	addr = BLOCK_ID(nextPhysicalBlock)*PHYSICAL_BLOCK_SIZE;
	//write the activeBlock to flash
	flashWriteBytes(addr, activeBlock, PHYSICAL_BLOCK_SIZE);
	blockHeaderCache[BLOCK_ID(nextPhysicalBlock)] = header | BLOCK_NOT_DELETED_BIT;
	blockMap[BLOCK_ID(header)] = nextPhysicalBlock | BLOCK_NOT_DELETED_BIT;

	//if the old physical block was different to the current
	//mark the old physical block as deleted
	if(BLOCK_ID(currentPhysicalBlock) != BLOCK_ID(nextPhysicalBlock)) {
		currentPhysicalBlock = currentPhysicalBlock & ~BLOCK_NOT_DELETED_BIT;
		addr = BLOCK_ID(currentPhysicalBlock)*PHYSICAL_BLOCK_SIZE;
		flashWriteBytes(addr, &currentPhysicalBlock, sizeof(currentPhysicalBlock));
		//the current block must point to the virtual block, where we took the next block from
		blockHeaderCache[BLOCK_ID(currentPhysicalBlock)] = usedVirtualBlock;
		blockMap[BLOCK_ID(usedVirtualBlock)] = currentPhysicalBlock;
		//erase the current physicalBlock
		flashBlockErase4K(addr);
	}

	activeBlockDirty = false;
	printCaches();
}

//returns the length of the virtual address space

long FlashWearLevelerBase::getSize() {
	//-1 to have at least one spare
	return blockCount-1 * VIRTUAL_BLOCK_SIZE;
}


long FlashWearLevelerBase::virtual2physicalAddr(long addr) {
	return CombinePhysicalAddress(SplitVirtualAddress(addr));
}


long FlashWearLevelerBase::physical2virtualAddr(long addr) {
	return CombineVirtualAddress(SplitPhysicalAddress(addr));
}
