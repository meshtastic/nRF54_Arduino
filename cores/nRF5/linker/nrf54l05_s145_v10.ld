/* nRF54L05 application with the s145 v10.0.1 SoftDevice.
 * RRAM 0x0-0x8000 is the bootloader (it owns reset), the SoftDevice sits at the top of RRAM. */
SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00008000, LENGTH = 0x3f000
  RAM (rwx) :  ORIGIN = 0x20004800, LENGTH = 0x3B780

  /* Bootloader peer data and double-reset marker, must stay untouched by the app */
  NOINIT (rwx) :  ORIGIN = 0x2003FF80, LENGTH = 0x78
}

__softdevice_start__ = 0x0005A800;

INCLUDE "nrf54_common.ld"
