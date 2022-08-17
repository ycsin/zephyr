/*
 * COPYRIGHT NOTICE: (c) 2022 Irnas.  All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/flash.h>
#include <drivers/spi.h>
#include <logging/log.h>
#include <pm/device.h>
#include <sys/byteorder.h>
#include <zephyr.h>

#ifdef CONFIG_NRFX_SPIM_EXT_MUTEX
#include <spi_external_mutex.h>
#endif

LOG_MODULE_REGISTER(spi_flash_en25, CONFIG_FLASH_LOG_LEVEL);

#define DT_DRV_COMPAT irnas_en25

/* EN25 commands used by this driver: */
/* - Main Memory Byte/Page Program through Buffer 1 without Built-In Erase */
/* - Ultra-Deep Power-Down */
#define CMD_ENTER_UDPD 0x79
/* - Buffer and Page Size Configuration, "Power of 2" binary page size */
#define CMD_BINARY_PAGE_SIZE                                                                       \
	{                                                                                          \
		0x3D, 0x2A, 0x80, 0xA6                                                             \
	} // should not be needed

/* ------------------------------*/
/* New commmads*/

/* - Reset Enable Command */
#define CMD_RESET_ENABLE     0x66
/* - Reset Command */
#define CMD_RESET	     0x99
/* - Write Enable Command */
#define CMD_WRITE_ENABLE     0x06
/* - Manufacturer and Device ID Read */
#define CMD_READ_ID	     0x9F
/* - Status Register Read Command */
#define CMD_READ_STATUS	     0x05
/* - Status Register Write Command */
#define CMD_WRITE_STATUS     0x01
/* - Chip Erase Command */
#define CMD_READ	     0x03
/* - Page Program (Continuous Write) Command */
#define CMD_PAGE_PROGRAM     0x02
/* - Chip erase Command */
#define CMD_CHIP_ERASE	     0xC7 /* It could also be 0x60 */
/* - Sector Erase Command */
#define CMD_SECTOR_ERASE     0x20
/* - Full Block Erase (64KB) Command */
#define CMD_FULL_BLOCK_ERASE 0xD8
/* - Full Block Erase (64KB) Command */
#define CMD_HALF_BLOCK_ERASE 0x52
/* - Page Erase */
#define CMD_PAGE_ERASE	     0x81
/* - Enter Deep Power-Down Command */
#define CMD_ENTER_DPD	     0xB9
/* - Exit from Deep Power-Down Command */
#define CMD_EXIT_DPD	     0xAB

#define INST_HAS_WP_OR(inst)  DT_INST_NODE_HAS_PROP(inst, wp_gpios) ||
#define ANY_INST_HAS_WP_GPIOS DT_INST_FOREACH_STATUS_OKAY(INST_HAS_WP_OR) 0

#define INST_HAS_HOLD_OR(inst)	DT_INST_NODE_HAS_PROP(inst, hold_gpios) ||
#define ANY_INST_HAS_HOLD_GPIOS DT_INST_FOREACH_STATUS_OKAY(INST_HAS_HOLD_OR) 0

#define STATUS_REG_WRITE_IN_PROGRESS 0x01

#define STATUS_REG_LSB_PAGE_SIZE_BIT 0x01

#define DEF_BUF_SET(_name, _buf_array)                                                             \
	const struct spi_buf_set _name = {                                                         \
		.buffers = _buf_array,                                                             \
		.count = ARRAY_SIZE(_buf_array),                                                   \
	}

struct spi_flash_en25_data {
	const struct device *spi;
	struct spi_cs_control spi_cs;
	struct k_sem lock;
#if IS_ENABLED(CONFIG_PM_DEVICE)
	uint32_t pm_state;
#endif
};

struct spi_flash_en25_config {
	const char *spi_bus;
	struct spi_config spi_cfg;
	const char *cs_gpio;
	gpio_pin_t cs_pin;
	gpio_dt_flags_t cs_dt_flags;
#if ANY_INST_HAS_WP_GPIOS
	const struct gpio_dt_spec *wp;
#endif
#if ANY_INST_HAS_HOLD_GPIOS
	const struct gpio_dt_spec *hold;
#endif
#if IS_ENABLED(CONFIG_FLASH_PAGE_LAYOUT)
	struct flash_pages_layout pages_layout;
#endif
	uint32_t chip_size;
	uint32_t sector_size;
	uint16_t block_size;
	uint16_t page_size;
	uint16_t t_enter_dpd; /* in microseconds */
	uint16_t t_exit_dpd;  /* in microseconds */
	bool use_udpd;
	uint8_t jedec_id[3];
};

static const struct flash_parameters flash_en25_parameters = {
	.write_block_size = 1,
	.erase_value = 0xff,
};

static struct spi_flash_en25_data *get_dev_data(const struct device *dev) { return dev->data; }

static const struct spi_flash_en25_config *get_dev_config(const struct device *dev)
{
	return dev->config;
}

static void acquire(const struct device *dev) { k_sem_take(&get_dev_data(dev)->lock, K_FOREVER); }

static void release(const struct device *dev) { k_sem_give(&get_dev_data(dev)->lock); }

static int acquire_ext_mutex(const struct device *dev)
{
#ifdef CONFIG_NRFX_SPIM_EXT_MUTEX
	int err = 0;
	// make sure lock is acquired by trying multiple times
	for (int i = 0; i < 5; i++) {
		err = spi_ext_mutex_acquire(get_dev_data(dev)->spi);
		if (err < 0) {
			spi_ext_mutex_release(get_dev_data(dev)->spi);
			k_sleep(K_MSEC(10));
		} else {
			break;
		}
	}
	if (err < 0) {
		return err;
	}
	LOG_DBG("Spi ext mutex acquired");
#endif
	return 0;
}

static int release_ext_mutex(const struct device *dev)
{
#ifdef CONFIG_NRFX_SPIM_EXT_MUTEX
	int err = spi_ext_mutex_release(get_dev_data(dev)->spi);
	if (err != 0) {
		LOG_ERR("spi_ext_mutex_release, err: %d", err);
		return err;
	}
	LOG_DBG("Spi ext mutex released");
#endif
	return 0;
}

#ifdef CONFIG_SPI_FLASH_EN25_JEDEC_CHECK_AT_INIT
static int check_jedec_id(const struct device *dev)
{
	const struct spi_flash_en25_config *cfg = get_dev_config(dev);
	int err;
	uint8_t const *expected_id = cfg->jedec_id;
	uint8_t read_id[sizeof(cfg->jedec_id)];
	const uint8_t opcode = CMD_READ_ID;
	const struct spi_buf tx_buf[] = {{
		.buf = (void *)&opcode,
		.len = sizeof(opcode),
	}};
	const struct spi_buf rx_buf[] = {{
						 .len = sizeof(opcode),
					 },
					 {
						 .buf = read_id,
						 .len = sizeof(read_id),
					 }};
	DEF_BUF_SET(tx_buf_set, tx_buf);
	DEF_BUF_SET(rx_buf_set, rx_buf);

	err = spi_transceive(get_dev_data(dev)->spi, &cfg->spi_cfg, &tx_buf_set, &rx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d/%u", err, __LINE__);
		return -EIO;
	}

	if (memcmp(expected_id, read_id, sizeof(read_id)) != 0) {
		LOG_ERR("Wrong JEDEC ID: %02X %02X %02X, "
			"expected: %02X %02X %02X",
			read_id[0], read_id[1], read_id[2], expected_id[0], expected_id[1],
			expected_id[2]);
		return -ENODEV;
	}

	return 0;
}
#endif

/*
 * Reads 1-byte Status Register
 */
static int read_status_register(const struct device *dev, uint8_t *status)
{
	int err;
	const uint8_t opcode = CMD_READ_STATUS;
	const uint8_t empty = 0;
	const struct spi_buf tx_buf[] = {{
						 .buf = (void *)&opcode,
						 .len = sizeof(opcode),
					 },
					 {
						 .buf = (void *)&empty,
						 .len = sizeof(uint8_t),
					 }};
	const struct spi_buf rx_buf[] = {{
						 .len = sizeof(opcode),
					 },
					 {
						 .buf = status,
						 .len = sizeof(uint8_t),
					 }};
	DEF_BUF_SET(tx_buf_set, tx_buf);
	DEF_BUF_SET(rx_buf_set, rx_buf);

	err = spi_transceive(get_dev_data(dev)->spi, &get_dev_config(dev)->spi_cfg, &tx_buf_set,
			     &rx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d/%u", err, __LINE__);
		return -EIO;
	}

	*status = sys_le16_to_cpu(*status);
	return 0;
}

static int wait_until_ready(const struct device *dev)
{
	int err;
	uint8_t status;

	for (int i = 0; i < CONFIG_SPI_FLASH_EN25_READY_TIMEOUT; i++) {
		err = read_status_register(dev, &status);
		if (err != 0 || !(status & STATUS_REG_WRITE_IN_PROGRESS)) {
			return err;
		}
		k_msleep(1);
	}

	// we are out of the loop so we have timed out
	return -ETIMEDOUT;
}

static int send_cmd_op(const struct device *dev, uint8_t opcode, uint32_t delay)
{
	int err = 0;
	const struct spi_buf tx_buf[] = {{
		.buf = (void *)&opcode,
		.len = sizeof(opcode),
	}};
	DEF_BUF_SET(tx_buf_set, tx_buf);

	err = spi_write(get_dev_data(dev)->spi, &get_dev_config(dev)->spi_cfg, &tx_buf_set);

	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d/%u", err, __LINE__);
		return -EIO;
	}

	k_busy_wait(delay);
	return 0;
}

static int perform_reset_sequence(const struct device *dev)
{
	int err;
	err = send_cmd_op(dev, CMD_RESET_ENABLE, 1);

	if (err != 0) {
		return err;
	}

	err = send_cmd_op(dev, CMD_RESET, 1);

	if (err != 0) {
		return err;
	}

	err = wait_until_ready(dev);
	return err;
}

static int set_write_enable(const struct device *dev)
{
	/* We add minimal delay of one microsecond, although datasheet says
	 * that it could be 30ns. */
	return send_cmd_op(dev, CMD_WRITE_ENABLE, 1);
}

static bool is_valid_request(off_t addr, size_t size, size_t chip_size)
{
	return (addr >= 0 && (addr + size) <= chip_size);
}

static int spi_flash_en25_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	const struct spi_flash_en25_config *cfg = get_dev_config(dev);
	int err;

	if (!is_valid_request(offset, len, cfg->chip_size)) {
		return -ENODEV;
	}

	uint8_t const op_and_addr[] = {
		CMD_READ,
		(offset >> 16) & 0xFF,
		(offset >> 8) & 0xFF,
		(offset >> 0) & 0xFF,
	};
	const struct spi_buf tx_buf[] = {{
		.buf = (void *)&op_and_addr,
		.len = sizeof(op_and_addr),
	}};
	const struct spi_buf rx_buf[] = {{
						 .len = sizeof(op_and_addr),
					 },
					 {
						 .buf = data,
						 .len = len,
					 }};
	DEF_BUF_SET(tx_buf_set, tx_buf);
	DEF_BUF_SET(rx_buf_set, rx_buf);

	int m_err = acquire_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	acquire(dev);
	err = spi_transceive(get_dev_data(dev)->spi, &cfg->spi_cfg, &tx_buf_set, &rx_buf_set);
	release(dev);

	m_err = release_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d/%u", err, __LINE__);
	}

	return (err != 0) ? -EIO : 0;
}

static int perform_write(const struct device *dev, off_t offset, const void *data, size_t len)
{
	int err;
	err = set_write_enable(dev);
	if (err != 0) {
		return err;
	}

	uint8_t const op_and_addr[] = {
		CMD_PAGE_PROGRAM,
		(offset >> 16) & 0xFF,
		(offset >> 8) & 0xFF,
		(offset >> 0) & 0xFF,
	};
	const struct spi_buf tx_buf[] = {{
						 .buf = (void *)&op_and_addr,
						 .len = sizeof(op_and_addr),
					 },
					 {
						 .buf = (void *)data,
						 .len = len,
					 }};
	DEF_BUF_SET(tx_buf_set, tx_buf);

	err = spi_write(get_dev_data(dev)->spi, &get_dev_config(dev)->spi_cfg, &tx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d/%u", err, __LINE__);
	} else {
		err = wait_until_ready(dev);
	}

	return (err != 0) ? -EIO : 0;
}

static int spi_flash_en25_write(const struct device *dev, off_t offset, const void *data,
				size_t len)
{
	const struct spi_flash_en25_config *cfg = get_dev_config(dev);
	int err = 0;

	if (!is_valid_request(offset, len, cfg->chip_size)) {
		return -ENODEV;
	}

	int m_err = acquire_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	acquire(dev);

	while (len) {
		size_t chunk_len = len;
		off_t current_page_start = offset - (offset & (cfg->page_size - 1));
		off_t current_page_end = current_page_start + cfg->page_size;

		if (chunk_len > (current_page_end - offset)) {
			chunk_len = (current_page_end - offset);
		}

		err = perform_write(dev, offset, data, chunk_len);
		if (err != 0) {
			break;
		}

		data = (uint8_t *)data + chunk_len;
		offset += chunk_len;
		len -= chunk_len;
	}

	release(dev);

	m_err = release_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	return err;
}

/* For this will stayed commented out as we dont need it */
// static int disable_block_protect(const struct device *dev)
//{
//     int err;
//     err = set_write_enable(dev);
//	if (err != 0) {
//         return err;
//     }
//
//	uint8_t const op_and_addr[] = {
//         CMD_PAGE_PROGRAM,
//		0b00111100,
//	};
//
//	const struct spi_buf tx_buf[] = {
//		{
//			.buf = (void *)&op_and_addr,
//			.len = sizeof(op_and_addr),
//		}
//	};
//
//	DEF_BUF_SET(tx_buf_set, tx_buf);
//
//	err = spi_write(get_dev_data(dev)->spi,
//			&get_dev_config(dev)->spi_cfg,
//			&tx_buf_set);
//	if (err != 0) {
//		LOG_ERR("SPI transaction failed with code: %d/%u",
//			err, __LINE__);
//	} else {
//		err = wait_until_ready(dev);
//	}
//
//	return (err != 0) ? -EIO : 0;
// }

static int perform_chip_erase(const struct device *dev)
{
	int err;
	err = set_write_enable(dev);
	if (err != 0) {
		return err;
	}

	uint8_t const chip_erase_cmd = CMD_CHIP_ERASE;
	const struct spi_buf tx_buf[] = {{
		.buf = (void *)&chip_erase_cmd,
		.len = sizeof(chip_erase_cmd),
	}};
	DEF_BUF_SET(tx_buf_set, tx_buf);

	err = spi_write(get_dev_data(dev)->spi, &get_dev_config(dev)->spi_cfg, &tx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d/%u", err, __LINE__);
	} else {
		err = wait_until_ready(dev);
	}

	return (err != 0) ? -EIO : 0;
}

static bool is_erase_possible(size_t entity_size, off_t offset, size_t requested_size)
{
	return (requested_size >= entity_size && (offset & (entity_size - 1)) == 0);
}

static int perform_erase_op(const struct device *dev, uint8_t opcode, off_t offset)
{
	int err;
	err = set_write_enable(dev);
	if (err != 0) {
		return err;
	}

	uint8_t const op_and_addr[] = {
		opcode,
		(offset >> 16) & 0xFF,
		(offset >> 8) & 0xFF,
		(offset >> 0) & 0xFF,
	};
	const struct spi_buf tx_buf[] = {{
		.buf = (void *)&op_and_addr,
		.len = sizeof(op_and_addr),
	}};
	DEF_BUF_SET(tx_buf_set, tx_buf);

	err = spi_write(get_dev_data(dev)->spi, &get_dev_config(dev)->spi_cfg, &tx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d/%u", err, __LINE__);
	} else {
		err = wait_until_ready(dev);
	}

	return (err != 0) ? -EIO : 0;
}

static int spi_flash_en25_erase(const struct device *dev, off_t offset, size_t size)
{
	const struct spi_flash_en25_config *cfg = get_dev_config(dev);
	int err = 0;

	if (!is_valid_request(offset, size, cfg->chip_size)) {
		return -ENODEV;
	}

	/* Diagnose region errors before starting to erase. */
	if (((offset % cfg->page_size) != 0) || ((size % cfg->page_size) != 0)) {
		return -EINVAL;
	}

	int m_err = acquire_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	acquire(dev);

	if (size == cfg->chip_size) {
		err = perform_chip_erase(dev);
	} else {
		while (size) {
			if (is_erase_possible(cfg->sector_size, offset, size)) {
				err = perform_erase_op(dev, CMD_SECTOR_ERASE, offset);
				offset += cfg->sector_size;
				size -= cfg->sector_size;
			} else if (is_erase_possible(cfg->block_size, offset, size)) {
				err = perform_erase_op(dev, CMD_FULL_BLOCK_ERASE, offset);
				offset += cfg->block_size;
				size -= cfg->block_size;
			} else if (is_erase_possible(cfg->page_size, offset, size)) {
				err = perform_erase_op(dev, CMD_HALF_BLOCK_ERASE, offset);
				offset += cfg->page_size;
				size -= cfg->page_size;
			} else {
				LOG_ERR("Unsupported erase request: "
					"size %zu at 0x%lx",
					size, (long)offset);
				err = -EINVAL;
			}

			if (err != 0) {
				break;
			}
		}
	}

	release(dev);

	m_err = release_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	return err;
}

#if IS_ENABLED(CONFIG_FLASH_PAGE_LAYOUT)
static void spi_flash_en25_pages_layout(const struct device *dev,
					const struct flash_pages_layout **layout,
					size_t *layout_size)
{
	*layout = &get_dev_config(dev)->pages_layout;
	*layout_size = 1;
}
#endif /* IS_ENABLED(CONFIG_FLASH_PAGE_LAYOUT) */

static int spi_flash_en25_init(const struct device *dev)
{
	struct spi_flash_en25_data *dev_data = get_dev_data(dev);
	const struct spi_flash_en25_config *dev_config = get_dev_config(dev);
	int err;

	dev_data->spi = device_get_binding(dev_config->spi_bus);
	if (!dev_data->spi) {
		LOG_ERR("Cannot find %s", dev_config->spi_bus);
		return -ENODEV;
	}

	/* GPIO configure */

#if ANY_INST_HAS_WP_GPIOS
	if (dev_config->wp) {
		if (gpio_pin_configure_dt(dev_config->wp, GPIO_OUTPUT_ACTIVE)) {
			LOG_ERR("Couldn't configure write protect pin");
			return -ENODEV;
		}
		gpio_pin_set(dev_config->wp->port, dev_config->wp->pin, 1);
	}
#endif

#if ANY_INST_HAS_HOLD_GPIOS
	if (dev_config->hold) {
		if (gpio_pin_configure_dt(dev_config->hold, GPIO_OUTPUT_ACTIVE)) {
			LOG_ERR("Couldn't configure hold pin");
			return -ENODEV;
		}
		gpio_pin_set(dev_config->hold->port, dev_config->hold->pin, 1);
	}
#endif

	if (dev_config->cs_gpio) {
		dev_data->spi_cs.gpio_dev = device_get_binding(dev_config->cs_gpio);
		if (!dev_data->spi_cs.gpio_dev) {
			LOG_ERR("Cannot find %s", dev_config->cs_gpio);
			return -ENODEV;
		}

		dev_data->spi_cs.gpio_pin = dev_config->cs_pin;
		dev_data->spi_cs.gpio_dt_flags = dev_config->cs_dt_flags;
		dev_data->spi_cs.delay = 0;
	}

	int m_err = acquire_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	acquire(dev);

	/* Perform reset sequence as we have seen that fetching
	 * JEDEC ID failed otherwise. */
	err = perform_reset_sequence(dev);
	if (err != 0) {
		LOG_ERR("perform_reset_sequence, err: %d", err);
		release_ext_mutex(dev);
		return err;
	}

	/* Just in case the chip was in the Deep (or Ultra-Deep) Power-Down
	 * mode, issue the command to bring it back to normal operation.
	 * Exiting from the Ultra-Deep mode requires only that the CS line
	 * is asserted for a certain time, so issuing the Resume from Deep
	 * Power-Down command will work in both cases.
	 */
	send_cmd_op(dev, CMD_EXIT_DPD, dev_config->t_exit_dpd);

	/* Check jedec ID */
#ifdef CONFIG_SPI_FLASH_EN25_JEDEC_CHECK_AT_INIT
	err = check_jedec_id(dev);
	if (err != 0) {
		return err;
	}
#else
	LOG_INF("SPI_FLASH_EN25_JEDEC_CHECK_AT_INIT is not set, skipping JEDEC ID check.");
#endif

	/* Place holder for function call, we might need it in future. */
	// err = disable_block_protect(dev);

	release(dev);

	m_err = release_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	return err;
}

#if IS_ENABLED(CONFIG_PM_DEVICE)
static int spi_flash_en25_pm_control(const struct device *dev, enum pm_device_action action)
{
	const struct spi_flash_en25_config *dev_config = get_dev_config(dev);

	int err = 0;
	int m_err = acquire_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}
	acquire(dev);

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		send_cmd_op(dev, CMD_EXIT_DPD, dev_config->t_exit_dpd);
		break;

	case PM_DEVICE_ACTION_SUSPEND:
		send_cmd_op(dev, CMD_ENTER_DPD, dev_config->t_enter_dpd);
		break;

	default:
		err = -ENOTSUP;
	}

	release(dev);

	m_err = release_ext_mutex(dev);
	if (m_err) {
		return m_err;
	}

	return err;
}
#endif /* IS_ENABLED(CONFIG_PM_DEVICE) */

static const struct flash_parameters *flash_en25_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);

	return &flash_en25_parameters;
}

static const struct flash_driver_api spi_flash_en25_api = {
	.read = spi_flash_en25_read,
	.write = spi_flash_en25_write,
	.erase = spi_flash_en25_erase,
	.get_parameters = flash_en25_get_parameters,
#if IS_ENABLED(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = spi_flash_en25_pages_layout,
#endif
};

#define DT_DRV_COMPAT irnas_en25

#define XSTR(x) STR(x)
#define STR(x)	#x

#define INST_HAS_WP_GPIO(idx) DT_NODE_HAS_PROP(DT_DRV_INST(idx), wp_gpios)

#define INST_WP_GPIO_SPEC(idx)                                                                     \
	IF_ENABLED(INST_HAS_WP_GPIO(idx), (static const struct gpio_dt_spec wp_##idx =             \
						   GPIO_DT_SPEC_GET(DT_DRV_INST(idx), wp_gpios);))

#define INST_HAS_HOLD_GPIO(idx) DT_NODE_HAS_PROP(DT_DRV_INST(idx), hold_gpios)

#define INST_HOLD_GPIO_SPEC(idx)                                                                   \
	IF_ENABLED(INST_HAS_HOLD_GPIO(idx),                                                        \
		   (static const struct gpio_dt_spec hold_##idx =                                  \
			    GPIO_DT_SPEC_GET(DT_DRV_INST(idx), hold_gpios);))

#define SPI_FLASH_EN25_INST(idx)                                                                   \
	enum {                                                                                     \
		INST_##idx##_BYTES = (DT_INST_PROP(idx, size) / 8),                                \
		INST_##idx##_PAGES = (INST_##idx##_BYTES / DT_INST_PROP(idx, page_size)),          \
	};                                                                                         \
	static struct spi_flash_en25_data inst_##idx##_data = {                                    \
		.lock = Z_SEM_INITIALIZER(inst_##idx##_data.lock, 1, 1),                           \
		IF_ENABLED(CONFIG_PM_DEVICE, (.pm_state = PM_DEVICE_STATE_ACTIVE))};               \
	INST_WP_GPIO_SPEC(idx)                                                                     \
	INST_HOLD_GPIO_SPEC(idx)                                                                   \
	static const struct spi_flash_en25_config inst_##idx##_config = {                          \
		.spi_bus = DT_INST_BUS_LABEL(idx),                                                 \
		.spi_cfg =                                                                         \
			{                                                                          \
				.frequency = DT_INST_PROP(idx, spi_max_frequency),                 \
				.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB |               \
					     SPI_WORD_SET(8) | SPI_LINES_SINGLE,                   \
				.slave = DT_INST_REG_ADDR(idx),                                    \
				.cs = &inst_##idx##_data.spi_cs,                                   \
			},                                                                         \
		IF_ENABLED(DT_INST_SPI_DEV_HAS_CS_GPIOS(idx),                                      \
			   (.cs_gpio = DT_INST_SPI_DEV_CS_GPIOS_LABEL(idx),                        \
			    .cs_pin = DT_INST_SPI_DEV_CS_GPIOS_PIN(idx),                           \
			    .cs_dt_flags = DT_INST_SPI_DEV_CS_GPIOS_FLAGS(idx), ))                 \
			IF_ENABLED(INST_HAS_WP_GPIO(idx), (.wp = &wp_##idx, )) IF_ENABLED(         \
				INST_HAS_HOLD_GPIO(idx), (.hold = &hold_##idx, ))                  \
				IF_ENABLED(CONFIG_FLASH_PAGE_LAYOUT,                               \
					   (.pages_layout =                                        \
						    {                                              \
							    .pages_count = INST_##idx##_PAGES,     \
							    .pages_size =                          \
								    DT_INST_PROP(idx, page_size),  \
						    }, ))                                          \
					.chip_size = INST_##idx##_BYTES,                           \
		.sector_size = DT_INST_PROP(idx, sector_size),                                     \
		.block_size = DT_INST_PROP(idx, block_size),                                       \
		.page_size = DT_INST_PROP(idx, page_size),                                         \
		.t_enter_dpd =                                                                     \
			ceiling_fraction(DT_INST_PROP(idx, enter_dpd_delay), NSEC_PER_USEC),       \
		.t_exit_dpd = ceiling_fraction(DT_INST_PROP(idx, exit_dpd_delay), NSEC_PER_USEC),  \
		.use_udpd = DT_INST_PROP(idx, use_udpd),                                           \
		.jedec_id = DT_INST_PROP(idx, jedec_id),                                           \
	};                                                                                         \
	IF_ENABLED(CONFIG_FLASH_PAGE_LAYOUT,                                                       \
		   (BUILD_ASSERT((INST_##idx##_PAGES * DT_INST_PROP(idx, page_size)) ==            \
					 INST_##idx##_BYTES,                                       \
				 "Page size specified for instance " #idx " of "                   \
				 "atmel,at45 is not compatible with its "                          \
				 "total size");))                                                  \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(idx, spi_flash_en25_init, spi_flash_en25_pm_control,                 \
			      &inst_##idx##_data, &inst_##idx##_config, POST_KERNEL,               \
			      CONFIG_SPI_FLASH_EN25_INIT_PRIORITY, &spi_flash_en25_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_FLASH_EN25_INST)
