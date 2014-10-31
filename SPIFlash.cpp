/*
 * Copyright (c) 2013 by Felix Rusu <felix@lowpowerlab.com>
 * SPI Flash memory library for arduino/moteino.
 * This works with 256byte/page SPI flash memory
 * For instance a 4MBit (512Kbyte) flash chip will have 2048 pages: 256*2048 = 524288 bytes (512Kbytes)
 * Minimal modifications should allow chips that have different page size but modifications
 * DEPENDS ON: Arduino SPI library
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or the GNU Lesser General Public License version 2.1, both as
 * published by the Free Software Foundation.
 */

#include <SPIFlash.h>
#include <SPI.h>

#define SPIFLASH_WRITEENABLE      0x06        // write enable
#define SPIFLASH_WRITEDISABLE     0x04        // write disable

#define SPIFLASH_BLOCKERASE_4K    0x20        // erase one 4K block of flash memory
#define SPIFLASH_BLOCKERASE_32K   0x52        // erase one 32K block of flash memory
#define SPIFLASH_BLOCKERASE_64K   0xD8        // erase one 64K block of flash memory
#define SPIFLASH_CHIPERASE        0x60        // chip erase (may take several seconds depending on size)
                                              // but no actual need to wait for completion (instead need to check the status register BUSY bit)
#define SPIFLASH_STATUSREAD       0x05        // read status register
#define SPIFLASH_STATUSWRITE      0x01        // write status register
#define SPIFLASH_ARRAYREAD        0x0B        // read array (fast, need to add 1 dummy byte after 3 address bytes)
#define SPIFLASH_ARRAYREADLOWFREQ 0x03        // read array (low frequency)

#define SPIFLASH_SLEEP            0xB9        // deep power down
#define SPIFLASH_WAKE             0xAB        // deep power wake up
#define SPIFLASH_BYTEPAGEPROGRAM  0x02        // write (1 to 256bytes)

#define SPIFLASH_AAI_PROGRAM	  0xAD		  // write in pairs of two bytes. needed by SST25V...

#define SPIFLASH_IDREAD           0x9F        // read JEDEC manufacturer and device ID (2 bytes, specific bytes for each manufacturer and device)
                                              // Example for Atmel-Adesto 4Mbit AT25DF041A: 0x1F44 (page 27: http://www.adestotech.com/sites/default/files/datasheets/doc3668.pdf)
                                              // Example for Winbond 4Mbit W25X40CL: 0xEF30 (page 14: http://www.winbond.com/NR/rdonlyres/6E25084C-0BFE-4B25-903D-AE10221A0929/0/W25X40CL.pdf)
#define SPIFLASH_MACREAD          0x4B        // read unique ID number (MAC)

//#define DBG()
//#define DBG()

byte SPIFlash::UNIQUEID[8];

/// IMPORTANT: NAND FLASH memory requires erase before write, because
///            it can only transition from 1s to 0s and only the erase command can reset all 0s to 1s
/// See http://en.wikipedia.org/wiki/Flash_memory
/// The smallest range that can be erased is a sector (4K, 32K, 64K); there is also a chip erase command

/// Constructor. JedecID is optional but recommended, since this will ensure that the device is present and has a valid response
/// get this from the datasheet of your flash chip
/// Example for Atmel-Adesto 4Mbit AT25DF041A: 0x1F44 (page 27: http://www.adestotech.com/sites/default/files/datasheets/doc3668.pdf)
/// Example for Winbond 4Mbit W25X40CL: 0xEF30 (page 14: http://www.winbond.com/NR/rdonlyres/6E25084C-0BFE-4B25-903D-AE10221A0929/0/W25X40CL.pdf)
SPIFlash::SPIFlash(uint8_t slaveSelectPin, uint16_t jedecID) {
  _slaveSelectPin = slaveSelectPin;
  _wantedJedecID = jedecID;
}

/// Select the flash chip
void SPIFlash::select() {
  noInterrupts();
  digitalWrite(_slaveSelectPin, LOW);
}

/// UNselect the flash chip
void SPIFlash::unselect() {
  digitalWrite(_slaveSelectPin, HIGH);
  interrupts();
}

/// setup SPI, read device ID etc...
boolean SPIFlash::initialize()
{
  pinMode(_slaveSelectPin, OUTPUT);
  unselect();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV2); //max speed, except on Due which can run at system clock speed
  SPI.begin();

  byte status=readStatus();
  if(status & 0x02) {
	  Serial.println("device initialized in WREN mode. resetting...");
	  select();
	  SPI.transfer(SPIFLASH_WRITEDISABLE);
	  unselect();
	  while(busy()){}
  }

  int i = 10;
  while(i-- > 0) {
  _deviceJedecID = readDeviceId();
  Serial.printf("Flash device id: %i %i\r\n", _deviceJedecID, _wantedJedecID);
  if(_deviceJedecID != 0) i=0;
  delay(100);
  }
  if(_deviceJedecID == 0) exit(1);


  if (_wantedJedecID == 0 || _deviceJedecID == _wantedJedecID) {
    command(SPIFLASH_STATUSWRITE, true); // Write Status Register
    SPI.transfer(0);                     // Global Unprotect
    unselect();
    return true;
  }
  return false;
}

/// Get the manufacturer and device ID bytes (as a short word)
uint16_t SPIFlash::readDeviceId()
{
  command(SPIFLASH_IDREAD); // Read JEDEC ID
  uint16_t jedecid = SPI.transfer(0) << 8;
  jedecid |= SPI.transfer(0);
  unselect();
  return jedecid;
}

/// Get the 64 bit unique identifier, stores it in UNIQUEID[8]. Only needs to be called once, ie after initialize
/// Returns the byte pointer to the UNIQUEID byte array
/// Read UNIQUEID like this:
/// flash.readUniqueId(); for (byte i=0;i<8;i++) { Serial.print(flash.UNIQUEID[i], HEX); Serial.print(' '); }
/// or like this:
/// flash.readUniqueId(); byte* MAC = flash.readUniqueId(); for (byte i=0;i<8;i++) { Serial.print(MAC[i], HEX); Serial.print(' '); }
byte* SPIFlash::readUniqueId()
{
  command(SPIFLASH_MACREAD);
  SPI.transfer(0);
  SPI.transfer(0);
  SPI.transfer(0);
  SPI.transfer(0);
  for (byte i=0;i<8;i++)
    UNIQUEID[i] = SPI.transfer(0);
  unselect();
  return UNIQUEID;
}

/// read 1 byte from flash memory
byte SPIFlash::readByte(long addr) {
  command(SPIFLASH_ARRAYREADLOWFREQ);
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  byte result = SPI.transfer(0);
  unselect();
  return result;
}

/// read unlimited # of bytes
void SPIFlash::readBytes(long addr, void* buf, word len) {
  command(SPIFLASH_ARRAYREAD);
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  SPI.transfer(0); //"dont care"
  for (word i = 0; i < len; ++i)
    ((byte*) buf)[i] = SPI.transfer(0);
  unselect();
}

/// Send a command to the flash chip, pass TRUE for isWrite when its a write command
void SPIFlash::command(byte cmd, boolean isWrite){
#if defined(__AVR_ATmega32U4__) // Arduino Leonardo, MoteinoLeo
  DDRB |= B00000001;            // Make sure the SS pin (PB0 - used by RFM12B on MoteinoLeo R1) is set as output HIGH!
  PORTB |= B00000001;
#endif
  while(busy()); //wait for any write/erase to complete

  if (isWrite)
  {
    command(SPIFLASH_WRITEENABLE); // Write Enable
    unselect();
  }

  select();
  SPI.transfer(cmd);
}

/// check if the chip is busy erasing/writing
boolean SPIFlash::busy()
{
  /*
  select();
  SPI.transfer(SPIFLASH_STATUSREAD);
  byte status = SPI.transfer(0);
  unselect();
  return status & 1;
  */
  return readStatus() & 1;
}

/// return the STATUS register
byte SPIFlash::readStatus()
{
  select();
  SPI.transfer(SPIFLASH_STATUSREAD);
  byte status = SPI.transfer(0);
  unselect();
  //Serial.printf("Status: 0x%hhx\r\n", status);
  return status;
}


/// Write 1 byte to flash memory
/// WARNING: you can only write to previously erased memory locations (see datasheet)
///          use the block erase commands to first clear memory (write 0xFFs)
void SPIFlash::writeByte(long addr, uint8_t byt) {
  command(SPIFLASH_BYTEPAGEPROGRAM, true);  // Byte/Page Program
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  SPI.transfer(byt);
  while(busy()){}
  unselect();
}

/// write 1-256 bytes to flash memory
/// WARNING: you can only write to previously erased memory locations (see datasheet)
///          use the block erase commands to first clear memory (write 0xFFs)
/// WARNING: if you write beyond a page boundary (or more than 256bytes),
///          the bytes will wrap around and start overwriting at the beginning of that same page
///          see datasheet for more details
void SPIFlash::writeBytes(long addr, const void* buf, int len) {
	//check for microchip SST25V...
  if(_deviceJedecID == 0xBF25) {
	byte* bytes = (byte*)buf;
  	//the SST25V must be written as double bytes
	//if the write starts at an uneven address, we need to split
	if(addr & 1) {
	  //Serial.println("AAI write uneven start");
	  writeByte(addr, bytes[0]);
	  bytes++;
	  addr++;
	  len--;
	}

	bool needAddress = true;

	//write byte pairs
	while(len >= 2) {
		if(needAddress) {
			//Serial.println("Start AAI");
			command(SPIFLASH_AAI_PROGRAM, true);
			SPI.transfer(addr >> 16);
			SPI.transfer(addr >> 8);
			SPI.transfer(addr);
			needAddress = false;
		} else {
			//Serial.println("Continue AAI");
			select();
			SPI.transfer(SPIFLASH_AAI_PROGRAM);
		}
		SPI.transfer(bytes[0]);
		SPI.transfer(bytes[1]);
		addr += 2;
		bytes += 2;
		len -= 2;
		unselect();
		//wait for finish
		while(busy()){}
	}

	//Serial.println("End AAI");
	select();
	SPI.transfer(SPIFLASH_WRITEDISABLE);
	while(busy()){}
	unselect();

	//write possible last byte
	if(len > 0) {
		//Serial.println("Write AAI trailing byte");
		writeByte(addr, bytes[0]);
	}

  } else {
    command(SPIFLASH_BYTEPAGEPROGRAM, true);  // Byte/Page Program
    SPI.transfer(addr >> 16);
    SPI.transfer(addr >> 8);
    SPI.transfer(addr);
    for (uint8_t i = 0; i < len; i++)
      SPI.transfer(((byte*) buf)[i]);
    unselect();
  }
}

/// erase entire flash memory array
/// may take several seconds depending on size, but is non blocking
/// so you may wait for this to complete using busy() or continue doing
/// other things and later check if the chip is done with busy()
/// note that any command will first wait for chip to become available using busy()
/// so no need to do that twice
void SPIFlash::chipErase() {
  command(SPIFLASH_CHIPERASE, true);
  unselect();
}

/// erase a 4Kbyte block
void SPIFlash::blockErase4K(long addr) {
  command(SPIFLASH_BLOCKERASE_4K, true); // Block Erase
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  unselect();
}

/// erase a 32Kbyte block
void SPIFlash::blockErase32K(long addr) {
  command(SPIFLASH_BLOCKERASE_32K, true); // Block Erase
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  unselect();
}

void SPIFlash::sleep() {
  command(SPIFLASH_SLEEP); // Block Erase
  unselect();
}

void SPIFlash::wakeup() {
  command(SPIFLASH_WAKE); // Block Erase
  unselect();
}

/// cleanup
void SPIFlash::end() {
  SPI.end();
}
