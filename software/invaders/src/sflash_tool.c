#include "at32f415.h"
#include "board.h"
#include "lt7680.h"

/* W25Q32 access over SWD. The host writes addr/len (and data for PROGRAM)
 * into sf_mbox, then cmd; the firmware runs it and sets status to
 * SF_DONE, or SF_ERROR. See tools/sflash.py.
 */
#define SF_BUF_SIZE 16384u
#define SF_MAGIC    0x544C4653u   /* "SFLT" */

enum { SF_CMD_NONE, SF_CMD_ID, SF_CMD_READ, SF_CMD_ERASE4K, SF_CMD_ERASE64K, SF_CMD_PROGRAM };
enum { SF_IDLE, SF_DONE, SF_ERROR };

volatile struct
{
  uint32_t magic;
  uint32_t cmd;
  uint32_t addr;
  uint32_t len;
  uint32_t status;
  uint32_t result;
  uint32_t count;
  uint8_t buf[SF_BUF_SIZE];
} sf_mbox;

static void sf_send_addr(uint8_t op, uint32_t addr)
{
  (void)lt_sf_xfer(op);
  (void)lt_sf_xfer((uint8_t)(addr >> 16));
  (void)lt_sf_xfer((uint8_t)(addr >> 8));
  (void)lt_sf_xfer((uint8_t)addr);
}

static uint8_t sf_status(void)
{
  uint8_t v;
  lt_sf_begin();
  (void)lt_sf_xfer(0x05u);
  v = lt_sf_xfer(0x00u);
  lt_sf_end();
  return v;
}

static void sf_write_enable(void)
{
  lt_sf_begin();
  (void)lt_sf_xfer(0x06u);
  lt_sf_end();
}

static bool sf_wait_ready(uint32_t timeout_ms)
{
  for(uint32_t t = 0; t < timeout_ms * 10u; t++)
  {
    if((sf_status() & 0x01u) == 0u)
    {
      return true;
    }
    board_delay_us(100u);
  }
  return false;
}

static uint32_t sf_id(void)
{
  uint32_t id;
  lt_sf_begin();
  (void)lt_sf_xfer(0x9Fu);
  id = (uint32_t)lt_sf_xfer(0u) << 16;
  id |= (uint32_t)lt_sf_xfer(0u) << 8;
  id |= lt_sf_xfer(0u);
  lt_sf_end();
  return id;
}

static void sf_read(uint32_t addr, uint32_t len)
{
  lt_sf_begin();
  sf_send_addr(0x03u, addr);
  for(uint32_t i = 0; i < len; i++)
  {
    sf_mbox.buf[i] = lt_sf_xfer(0u);
  }
  lt_sf_end();
}

static bool sf_erase(uint8_t op, uint32_t addr, uint32_t timeout_ms)
{
  sf_write_enable();
  lt_sf_begin();
  sf_send_addr(op, addr);
  lt_sf_end();
  return sf_wait_ready(timeout_ms);
}

static bool sf_program(uint32_t addr, uint32_t len)
{
  uint32_t done = 0u;

  while(done < len)
  {
    /* Page program must not cross a 256-byte page. */
    uint32_t n = 256u - ((addr + done) & 0xFFu);
    if(n > len - done)
    {
      n = len - done;
    }
    sf_write_enable();
    lt_sf_begin();
    sf_send_addr(0x02u, addr + done);
    for(uint32_t i = 0; i < n; i++)
    {
      (void)lt_sf_xfer(sf_mbox.buf[done + i]);
    }
    lt_sf_end();
    if(!sf_wait_ready(10u))
    {
      return false;
    }
    done += n;
  }
  return true;
}

int main(void)
{
  board_gpio_init();
  board_power_hold(true);
  board_clock_init();
  if(!lt_init())
  {
    for(;;)
    {
    }
  }
  lt_display_on(true);
  lt_sf_init();
  sf_mbox.result = sf_id();
  sf_mbox.magic = SF_MAGIC;

  for(;;)
  {
    uint32_t cmd = sf_mbox.cmd;
    uint32_t addr = sf_mbox.addr;
    uint32_t len = sf_mbox.len;
    bool ok = true;

    if(cmd == SF_CMD_NONE)
    {
      continue;
    }
    if(len > SF_BUF_SIZE || addr + len > 0x400000u)
    {
      ok = false;
    }
    else
    {
      switch(cmd)
      {
        case SF_CMD_ID: sf_mbox.result = sf_id(); break;
        case SF_CMD_READ: sf_read(addr, len); break;
        case SF_CMD_ERASE4K: ok = sf_erase(0x20u, addr, 500u); break;
        case SF_CMD_ERASE64K: ok = sf_erase(0xD8u, addr, 3000u); break;
        case SF_CMD_PROGRAM: ok = sf_program(addr, len); break;
        default: ok = false; break;
      }
    }
    sf_mbox.count++;
    sf_mbox.cmd = SF_CMD_NONE;
    sf_mbox.status = ok ? SF_DONE : SF_ERROR;
  }
}
