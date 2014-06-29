#include "FlashWearLeveler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BLOCK_DELETED_BIT (1<<15)

//masks the last 14bits. the first two bits are reserved as flags
#define BLOCK_ID(v) ((v) & 0x3fff)

//block is deleted, if bit 15 is 0. This
#define BLOCK_DELETED(v) ( !((v) & (1<<15)) )
//block is free, if it is either 0xffff or the deleted bit is 0
#define BLOCK_IS_FREE(v) ((v) == 0xffff || !( (v) & (1<<15) ) )

#define VIRTUAL_BLOCK_SIZE 4094
#define PHYSICAL_BLOCK_SIZE 4096

//represents an address as block index and offset into the block
typedef struct {
	uint16_t block;
	uint16_t offset;
} addr_info;

const uint16_t ErasedHeader = 0xffff;

//#define FWL_ERR(...) Serial.printf(__VA_ARGS__); Serial.println("");
#define FWL_ERR(...) printf(__VA_ARGS__); printf("\n");

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


template<typename Flash>
FlashWearLeveler<Flash>::FlashWearLeveler(Flash& fl, uint16_t noOf4kBlocks):flash(fl), blockCount(noOf4kBlocks) {
	blockMap = (uint16_t*)malloc(noOf4kBlocks * sizeof(uint16_t));
	assert(blockMap != 0);
	blockHeaderCache = (uint16_t*)malloc(noOf4kBlocks * sizeof(uint16_t));
	assert(blockHeaderCache != 0);
}

FlashWearLeveler::~FlashWearLeveler() {
	free(blockMap);
	blockMap = 0;
	free(blockHeaderCache);
	blockHeaderCache = 0;
}

boolean FlashWearLeveler::initialize() {
	if(!flash.initialize()) return false;
	activeBlockDirty = false;

	//clean the current block cache
	memset(activeBlock, 0xFF, PHYSICAL_BLOCK_SIZE);

	//initialize the map with ff (unused)
	memset(blockMap, 0xFF, noOf4kBlocks * sizeof(uint16_t));

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
				//flash.blockErase4K(i*4096);
				continue;
			}

			//mark as NOT deleted by setting the deleted bit
			blockMap[BLOCK_ID(virtualBlockId)] = (i | BLOCK_DELETED_BIT);
		}
	}

	//now iterate over the virtual blocks to fill the holes
	for(i=0; i<blockCount; i++) {
		int lastFreePhysicalBlock = 0;
		//if it is marked as free, lets assign a physical block
		if(blockMap[i] == 0xffff) {

			int y;
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

			//we should never end up here
			FWL_ERR("Didn't find enough free blocks");
			return false;
		}
	}
}

byte FlashWearLeveler::readByte(long addr) {
	uint16_t h = getActiveBlockHeader();
	if(!BLOCK_IS_FREE(h)) {
		//active block is valid, see, if we need to read from there

		//check if the addr lies in the activeBlock
		addr_info i = SplitVirtualAddress(addr);
		if(i.block == BLOCK_ID(h)) {
			return activeBlock[i.offset];
		}
	}

	//just forward
	return flash.readByte(addr);
}

void FlashWearLeveler::readBytes(long addr, void* buf, word len) {
	//iterate over the blocks
	addr_info start = SplitVirtualAddress(addr);
	addr_info end = SplitVirtualAddress(addr + len);

	int pos = 0;
	while(start != end) {
		long physicalAddress = CombinePhysicalAddress(start);
		int len;
		if(end.block > start.block) {
			//copy the rest
			len = VIRTUAL_BLOCK_SIZE - start.offset;
			readBytesFromVBlock(start, buf, len);
			start.block++;
			start.offset=0;
		} else {
			len = end.offset - start.offset;
			readBytesFromVBlock(start, buf, len);
			start.offset = end.offset;
		}
		(byte*)buf += len;
	}
}

void FlashWearLeveler::readBytesFromVBlock(addr_info virtualStartInfo, void* buf, word len) {
	assert(virtualStartInfo.offset + len < VIRTUAL_BLOCK_SIZE);
	uint16_t h = getActiveBlockHeader();
	//se if we need to copy from the active Block
	if(!BLOCK_IS_FREE(h) && BLOCK_ID(h) == virtualStartInfo.block) {
		memcpy(buf, activeBlock + virtualStartInfo.offset, len);
	} else {
		addr_info physicalInfo;
		physicalInfo.block = BLOCK_ID(blockMap[virtualStartInfo.block]);
		physicalInfo.offset = virtualStartInfo.offset;
		flash.readBytes(CombinePhysicalAddress(physicalInfo), buf, len);
	}
}

void FlashWearLeveler::writeByte(long addr, byte byt) {
	addr_info virtualInfo = SplitVirtualAddress(addr);
	activateVirtualBlock(virtualInfo.block);

	activeBlock[virtualInfo.offset] = byt;
	activeBlockDirty = true;
}

void FlashWearLeveler::writeBytes(long addr, const void* buf, int len) {
	addr_info start = SplitVirtualAddress(addr);
	addr_info end = SplitVirtualAddress(addr + len);

	while(start != end) {
		activateVirtualBlock(start.block);
		if(end.block > start.block) {
			//copy the rest
			len = VIRTUAL_BLOCK_SIZE - start.offset;
			memcpy(activeBlock + start.offset, buf, len);
			start.block++;
			start.offset=0;
		} else {
			len = end.offset - start.offset;
			memcpy(activeBlock + start.offset, buf, len);
			start.offset = end.offset;
		}
		activeBlockDirty = true;
		(byte*)buf += len;
	}
}

void FlashWearLeveler::activateVirtualBlock(uint16_t virtualBlockHeader) {
	uint16_t header = getActiveBlockHeader();
	if(BLOCK_ID(virtualBlockHeader) != BLOCK_ID(header)) {
		flush();
		uint16_t physicalBlockHeader = blockMap[BLOCK_ID(virtualBlockHeader)];
		flash.readBytes(BLOCK_ID(physicalBlockHeader)*PHYSICAL_BLOCK_SIZE, activeBlock, 4096);
		//it might be that we load a erased flash page, were the header would be 0xffff. Lets correct that
		((uint16_t*)activeBlock)[0] = blockHeaderCache[BLOCK_ID(physicalBlockHeader)];
	}
}

uint16_t FlashWearLeveler::readBlockHeader(uint16_t physicalBlockId) {
	uint16_t res;
	flash.readBytes(physicalBlockId*PHYSICAL_BLOCK_SIZE, &res, sizeof(uint16_t));
	return res;
}

uint16_t FlashWearLeveler::getActiveBlockHeader() {
	return ((uint16_t*)activeBlock)[0];
}

bool FlashWearLeveler::flushNeeded() {
	return activeBlockDirty;
}

void FlashWearLeveler::flush() {
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
	flash.writeBytes(addr, activeBlock, PHYSICAL_BLOCK_SIZE);
	blockHeaderCache[BLOCK_ID(nextPhysicalBlock)] = header;
	blockMap[BLOCK_ID(header)] = nextPhysicalBlock | BLOCK_DELETED_BIT;

	//if the old physical block was different to the current
	//mark the old physical block as deleted
	if(BLOCK_ID(currentPhysicalBlock) != BLOCK_ID(nextPhysicalBlock)) {
		currentPhysicalBlock = currentPhysicalBlock & ~BLOCK_DELETED_BIT;
		addr = BLOCK_ID(currentPhysicalBlock)*PHYSICAL_BLOCK_SIZE;
		flash.writeBytes(addr, &currentPhysicalBlock, sizeof(currentPhysicalBlock));
		//the current block must point to the virtual block, where we took the next block from
		blockHeaderCache[BLOCK_ID(currentPhysicalBlock)] = usedVirtualBlock;
		blockMap[BLOCK_ID(usedVirtualBlock)] = currentPhysicalBlock;
		//erase the current physicalBlock
		flash.blockErase4K(addr);
	}

	activeBlockDirty = false;
}

//returns the length of the virtual address space
long FlashWearLeveler::getSize() {
	//-1 to have at least one spare
	return blockCount-1 * VIRTUAL_BLOCK_SIZE;
}

long FlashWearLeveler::virtual2physicalAddr(long addr) {
	return CombinePhysicalAddress(SplitVirtualAddress(addrs));
}

long FlashWearLeveler::physical2virtualAddr(long addr) {
	return CombineVirtualAddress(SplitPhysicalAddress(addr));
}
