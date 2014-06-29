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

#ifndef _SPIFLASH_H_
#define _SPIFLASH_H_

#if ARDUINO >= 100
#include <Arduino.h>
#else
#include <wiring.h>
#include "pins_arduino.h"
#endif

/// IMPORTANT: NAND FLASH memory requires erase before write, because
///            it can only transition from 1s to 0s and only the erase command can reset all 0s to 1s
/// See http://en.wikipedia.org/wiki/Flash_memory
/// The smallest range that can be erased is a sector (4K, 32K, 64K); there is also a chip erase command

/// Standard SPI flash commands
/// Assuming the WP pin is pulled up (to disable hardware write protection)
/// To use any write commands the WEL bit in the status register must be set to 1.
/// This is accomplished by sending a 0x06 command before any such write/erase command.
/// The WEL bit in the status register resets to the logical “0” state after a
/// device power-up or reset. In addition, the WEL bit will be reset to the logical “0” state automatically under the following conditions:
/// • Write Disable operation completes successfully
/// • Write Status Register operation completes successfully or aborts
/// • Protect Sector operation completes successfully or aborts
/// • Unprotect Sector operation completes successfully or aborts
/// • Byte/Page Program operation completes successfully or aborts
/// • Sequential Program Mode reaches highest unprotected memory location
/// • Sequential Program Mode reaches the end of the memory array
/// • Sequential Program Mode aborts
/// • Block Erase operation completes successfully or aborts
/// • Chip Erase operation completes successfully or aborts
/// • Hold condition aborts
                                              
class SPIFlash {
public:
  static byte UNIQUEID[8];
  SPIFlash(byte slaveSelectPin, uint16_t jedecID=0);
  boolean initialize();
  void command(byte cmd, boolean isWrite=false);
  byte readStatus();
  byte readByte(long addr);
  void readBytes(long addr, void* buf, word len);
  void writeByte(long addr, byte byt);
  void writeBytes(long addr, const void* buf, int len);
  boolean busy();
  void chipErase();
  void blockErase4K(long address);
  void blockErase32K(long address);
  uint16_t readDeviceId();
  byte* readUniqueId();
  
  void sleep();
  void wakeup();
  void end();
protected:
  void select();
  void unselect();
  byte _slaveSelectPin;
  uint16_t _wantedJedecID;
  uint16_t _deviceJedecID;
};

#endif
