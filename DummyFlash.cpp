
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "DummyFlash.h"


#define BLOCK_SIZE 4096

#define MAX_ADDR (blockCount * 4096)

DummyFlash::DummyFlash(int _blockCount):blockCount(_blockCount) {
	printf("size: %i\n", (int)sizeof(struct dummyblock_t));
	assert(sizeof(struct dummyblock_t) == 4096);
	data = (struct dummyblock_t*)malloc(blockCount * sizeof(struct dummyblock_t));
	assert(data);
	eraseCounter = (int*)calloc(blockCount, sizeof(int));
}

DummyFlash::~DummyFlash() {
	free(data);
	free(eraseCounter);
}

uint8_t DummyFlash::readByte(long addr) {
	assert(addr < MAX_ADDR);
	return ((uint8_t*)data)[addr];
}

void DummyFlash::readBytes(long addr, void* buf, long len) {
	assert(addr + len <= MAX_ADDR);
	memcpy(buf, ((uint8_t*)data)+addr, len);
}
void DummyFlash::writeByte(long addr, uint8_t byt) {
	assert(addr < MAX_ADDR);
	//only change ones to zeros, zerosuint8_tt stay
	uint8_t old = ((uint8_t*)data)[addr];
	((uint8_t*)data)[addr] = byt & old;
}

void DummyFlash::writeBytes(long addr, const void* buf, int len) {
	assert(addr + len <= MAX_ADDR);
	uint8_t* bytes = (uint8_t*)buf;
	while(len-- > 0) {
		writeByte(addr++, *bytes);
		bytes++;
	}
}

void DummyFlash::chipErase() {
	for(int i=0; i<blockCount; i++) {
		blockErase4K(i * BLOCK_SIZE);
	}
}

void DummyFlash::blockErase4K(long address) {
	long block = address/4096;
	memset(((uint8_t*)data) + block*4096, 0xff, 4096);
	eraseCounter[block]++;
}

void DummyFlash::printWearLevel() {
	printf("Wear level: \n");
	for(int i = 0; i<blockCount; i++) {
		printf("%03i: %i\n", i, eraseCounter[i]);
	}
	printf("\n\n");
}

/*void DummyFlash::blockErase32K(long address) {
	address &= ~(32768-1);
	for(int i=0; i<8; i++) {
		blockErase4k(address + i * BLOCK_SIZE);
	}
}*/

