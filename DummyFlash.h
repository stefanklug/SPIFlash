#ifndef _DUMMY_FLASH_
#define _DUMMY_FLASH_

#include <stdint.h>

class DummyFlash {
public:
	DummyFlash(int blockCount);
	~DummyFlash();
	uint8_t readByte(long addr);
	void readBytes(long addr, void* buf, long len);
	void writeByte(long addr, uint8_t byt);
	void writeBytes(long addr, const void* buf, int len);
	void chipErase();
	void blockErase4K(long address);
	//void blockErase32K(long address);

	void printWearLevel();
  protected:
	uint8_t* data;
	int blockCount;
	int* eraseCounter;
};

#endif
