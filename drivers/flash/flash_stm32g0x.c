/*
 * Copyright (c) 2019 Philippe Retornaz <philippe@shapescale.com>
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2017 BayLibre, SAS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_DOMAIN flash_stm32g0
#define LOG_LEVEL CONFIG_FLASH_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(LOG_DOMAIN);

#include <kernel.h>
#include <device.h>
#include <string.h>
#include <drivers/flash.h>
#include <init.h>
#include <soc.h>

#include "flash_stm32.h"


#define STM32G0X_PAGE_SHIFT	11

#if defined(FLASH_DBANK_SUPPORT)
#define STM32G0X_BANK2_PAGE_START 256
#define STM32G0X_BANK2_PAGE_OFFSET STM32G0X_BANK2_PAGE_START - FLASH_PAGE_NB
#endif

/*
 * offset and len must be aligned on 8 for write,
 * positive and not beyond end of flash
 */
bool flash_stm32_valid_range(const struct device *dev, off_t offset,
			     uint32_t len,
			     bool write)
{
	return (!write || (offset % 8 == 0 && len % 8 == 0)) &&
		flash_stm32_range_exists(dev, offset, len);
}

/*
 * Calculate the page number from an offset
 * Each page is 2Kbytes long.
 * Single bank device starts from page 0 and ends at page 127 max.
 * Dual bank device's bank 2 starts from page 256 and ends at page 383 max.
 */
static unsigned int get_page(off_t offset)
{
	unsigned int page = offset >> STM32G0X_PAGE_SHIFT;

	return (page >= FLASH_PAGE_NB) ?
		page + STM32G0X_BANK2_PAGE_OFFSET : page;
}

static unsigned int get_bank(unsigned int page)
{
#if defined(FLASH_DBANK_SUPPORT)
	return (page >= FLASH_PAGE_NB) ? FLASH_BANK_2 : FLASH_BANK_1;
#else
	ARG_UNUSED(page);
	return FLASH_BANK_1;
#endif
}

static inline void flush_cache(FLASH_TypeDef *regs)
{
	if (regs->ACR & FLASH_ACR_ICEN) {
		regs->ACR &= ~FLASH_ACR_ICEN;
		/* Datasheet: ICRST: Instruction cache reset :
		 * This bit can be written only when the instruction cache
		 * is disabled
		 */
		regs->ACR |= FLASH_ACR_ICRST;
		regs->ACR &= ~FLASH_ACR_ICRST;
		regs->ACR |= FLASH_ACR_ICEN;
	}
}

static int write_dword(const struct device *dev, off_t offset, uint64_t val)
{
	volatile uint32_t *flash = (uint32_t *)(offset + CONFIG_FLASH_BASE_ADDRESS);
	FLASH_TypeDef *regs = FLASH_STM32_REGS(dev);
	uint32_t tmp;
	int rc;

	/* if the control register is locked, do not fail silently */
	if (regs->CR & FLASH_CR_LOCK) {
		return -EIO;
	}

	/* Check that no Flash main memory operation is ongoing */
	rc = flash_stm32_wait_flash_idle(dev);
	if (rc < 0) {
		return rc;
	}

	/* Check if this double word is erased */
	if (flash[0] != 0xFFFFFFFFUL ||
	    flash[1] != 0xFFFFFFFFUL) {
		return -EIO;
	}

	/* Set the PG bit */
	regs->CR |= FLASH_CR_PG;

	/* Flush the register write */
	tmp = regs->CR;

	/* Perform the data write operation at the desired memory address */
	flash[0] = (uint32_t)val;
	flash[1] = (uint32_t)(val >> 32);

	/* Wait until the BSY bit is cleared */
	rc = flash_stm32_wait_flash_idle(dev);

	/* Clear the PG bit */
	regs->CR &= (~FLASH_CR_PG);

	return rc;
}

static int erase_page(const struct device *dev, unsigned int page)
{
	FLASH_TypeDef *regs = FLASH_STM32_REGS(dev);
	uint32_t tmp;
	int rc;

	/* if the control register is locked, do not fail silently */
	if (regs->CR & FLASH_CR_LOCK) {
		return -EIO;
	}

	/* Check that no Flash memory operation is ongoing */
	rc = flash_stm32_wait_flash_idle(dev);
	if (rc < 0) {
		return rc;
	}

	/*
	 * If an erase operation in Flash memory also concerns data
	 * in the instruction cache, the user has to ensure that these data
	 * are rewritten before they are accessed during code execution.
	 */
	flush_cache(regs);

	/* Get configuration register, then clear page number */
	tmp = (regs->CR & ~FLASH_CR_PNB);

#if defined(FLASH_DBANK_SUPPORT)
	/* Check if page has to be erased in bank 1 or 2 */
	if (get_bank(page) != FLASH_BANK_1) {
		tmp |= FLASH_CR_BKER;
	} else {
		tmp &= ~FLASH_CR_BKER;
	}
#endif

	tmp |= ((FLASH_CR_STRT | (page <<  FLASH_CR_PNB_Pos) | FLASH_CR_PER));
	regs->CR = tmp;

	/* Wait for the BSY bit */
	rc = flash_stm32_wait_flash_idle(dev);

	regs->CR &= ~FLASH_CR_PER;

	return rc;
}

int flash_stm32_block_erase_loop(const struct device *dev,
				 unsigned int offset,
				 unsigned int len)
{
	int i, rc = 0;

	i = get_page(offset);
	for (; i <= get_page(offset + len - 1) ; ++i) {
		rc = erase_page(dev, i);
		if (rc < 0) {
			break;
		}
	}

	return rc;
}

int flash_stm32_write_range(const struct device *dev, unsigned int offset,
			    const void *data, unsigned int len)
{
	int i, rc = 0;

	for (i = 0; i < len; i += 8, offset += 8) {
		rc = write_dword(dev, offset,
				UNALIGNED_GET((const uint64_t *) data + (i >> 3)));
		if (rc < 0) {
			return rc;
		}
	}

	return rc;
}

void flash_stm32_page_layout(const struct device *dev,
			     const struct flash_pages_layout **layout,
			     size_t *layout_size)
{
	ARG_UNUSED(dev);
	int i;

#if defined(FLASH_DBANK_SUPPORT)
	static struct flash_pages_layout stm32g0_flash_layout[2];
#else
	static struct flash_pages_layout stm32g0_flash_layout[1];
#endif

	for (i = 0; i < FLASH_BANK_NB; i++) {
		stm32g0_flash_layout[i].pages_count = 0;
		stm32g0_flash_layout[i].pages_size = 0;

		if (stm32g0_flash_layout[i].pages_count == 0) {
			stm32g0_flash_layout[i].pages_count = FLASH_PAGE_NB;
			stm32g0_flash_layout[i].pages_size = FLASH_PAGE_SIZE;
		}
	}

	*layout = stm32g0_flash_layout;
	*layout_size = FLASH_BANK_NB;
}
