#include "../DummyFlash.h"
#include "../FlashWearLeveler.h"
#include "stdio.h"
#include <stdlib.h>
#include <string.h>

DummyFlash flash(8);
FlashWearLeveler<DummyFlash> leveler(flash, 8);

const char* t1="Hallo Welt";
const char* t2="The quick brown fox jumps over the lazy dog!";
const char* t3="Foo Bar";

void testWRiteByte1() {
	leveler.format();
	uint8_t x=0;
	for(int i=0;i<1000;i++) {
		x++;
		leveler.writeByte(0x01, x);
		leveler.flush();
	}

	flash.printWearLevel();
}

void verifyString(long addr, const char* str) {
	int l = strlen(str);
	char* d = (char*)malloc(l+1);
	leveler.readBytes(addr, d, l+1);
	printf("expected: %s\n", str);
	printf("got     : %s\n", d);
	if(strcmp(str, d) != 0) {
		printf("failed!\n");
		exit(1);
	}
	printf("\n");
	free(d);
}

void writeString(long addr, const char* str) {
	leveler.writeBytes(addr, str, strlen(str)+1 );
}

void testAlternatingWrites() {
	leveler.format();
	uint8_t x=0;
	for(int i=0;i<1000;i++) {
		writeString(1, t1);
		writeString(4000, t2);

		writeString(8*4000, t3);
		leveler.flush();
		leveler.initialize();
	}

	verifyString(1, t1);
	verifyString(4000, t2);
	verifyString(8*4000, t3);

	flash.printWearLevel();
}

void testSimpleWrite() {
	leveler.format();
	writeString(0, t1);
	verifyString(0, t1);

	writeString(0, t2);
	verifyString(0, t2);

	writeString(4100, t1);
	verifyString(4100, t1);

	writeString(0, t2);
	verifyString(0, t2);

	verifyString(4100, t1);

	flash.printWearLevel();
}

int main(int argc, const char** argv) {
	testSimpleWrite();
	testAlternatingWrites();
}
