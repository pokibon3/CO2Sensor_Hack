#ifndef AT32F415_CONF_H
#define AT32F415_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HEXT_VALUE
#define HEXT_VALUE ((uint32_t)8000000)
#endif

#define HEXT_STARTUP_TIMEOUT ((uint16_t)0x3000)
#define HICK_VALUE           ((uint32_t)8000000)
#define LEXT_VALUE           ((uint32_t)32768)

#define CRM_MODULE_ENABLED
#define GPIO_MODULE_ENABLED
#define SPI_MODULE_ENABLED
#define MISC_MODULE_ENABLED
#define FLASH_MODULE_ENABLED
#define DMA_MODULE_ENABLED
#define TMR_MODULE_ENABLED

#include "at32f415_crm.h"
#include "at32f415_gpio.h"
#include "at32f415_spi.h"
#include "at32f415_misc.h"
#include "at32f415_flash.h"
#include "at32f415_dma.h"
#include "at32f415_tmr.h"

#ifdef __cplusplus
}
#endif

#endif
