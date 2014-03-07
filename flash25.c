
// TODO: add "based on 25xx.c from ChibiOS-EEPROM"
// TODO: check support for non sst25 devices

#include "flash25.h"

/* SST25 command set */
#define CMD_READ		0x03
#define CMD_FAST_READ		0x0b
#define CMD_ERASE_4K		0x20
#define CMD_ERASE_32K		0x52
#define CMD_ERASE_64K		0xd8
#define CMD_CHIP_ERASE		0x60 /* or 0xc7 */
#define CMD_BYTE_PROG		0x02
#define CMD_AAI_WORD_PROG	0xad
#define CMD_RDSR		0x05
#define CMD_EWSR		0x50
#define CMD_WRSR		0x01
#define CMD_WREN		0x06
#define CMD_WRDI		0x04
#define CMD_RDID		0x90
#define CMD_JDEC_ID		0x9f
#define CMD_EBSY		0x70
#define CMD_DBSY		0x80

/* SST25 status register bits */
#define STAT_BUSY		(1<<0)
#define STAT_WEL		(1<<1)
#define STAT_BP0		(1<<2)
#define STAT_BP1		(1<<3)
#define STAT_BP2		(1<<4)
#define STAT_BP3		(1<<5)
#define STAT_AAI		(1<<6)
#define STAT_BPL		(1<<7)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr)	(sizeof(arr)/sizeof(arr[0]))
#endif

#define DEBUG_FLASH25
#ifdef DEBUG_FLASH25
#include "chprintf.h"
#define dbgPrint(fmt, args...)	chprintf(&SD1, "%s: " fmt "\n", __func__, ##args)
#else
#define dbgPrint(fmt, args...)
#endif

#define FLASH_TIMEOUT	MS2ST(10)
#define ERASE_TIMEOUT	MS2ST(100)

/*
 * Supported device table
 */

#define INFO(name_, id_, ps_, es_, nr_)		{ /*name_,*/ id_, ps_, es_, nr_ }
struct flash_info {
	/*const char *name;*/
	uint32_t jdec_id;
	uint16_t page_size;
	uint16_t erase_size;
	uint16_t nr_pages;
};

static const struct flash_info flash_info_table[] = {
	INFO("sst25vf016b", 0xbf2541, 256, 4096, 16*1024*1024/8/256),
	INFO("sst25vf032b", 0xbf254a, 256, 4096, 32*1024*1024/8/256)
};

/*
 * Low level flash interface
 */

/**
 * @brief SPI-Flash transfer function
 * @notapi
 */
static void flash_transfer(const Flash25Config *cfg,
		const uint8_t *txbuf, size_t txlen,
		uint8_t *rxbuf, size_t rxlen)
{
#if SPI_USE_MUTUAL_EXCLUSION
	spiAcquireBus(cfg->spip);
#endif

	spiStart(cfg->spip, cfg->spicfg);
	spiSelect(cfg->spip);
	spiSend(cfg->spip, txlen, txbuf);
	if (rxlen)
		spiReceive(cfg->spip, rxlen, rxbuf);
	spiUnselect(cfg->spip);

#if SPI_USE_MUTUAL_EXCLUSION
	spiReleaseBus(cfg->spip);
#endif
}

/**
 * @brief checks busy flag
 * @notapi
 */
static bool_t flash_is_busy(const Flash25Config *cfg)
{
	uint8_t cmd = CMD_RDSR;
	uint8_t stat;

	flash_transfer(cfg, &cmd, 1, &stat, 1);
	dbgPrint("sr: %02x", stat);
	return !!(stat & STAT_BUSY);
}

/**
 * @brief wait write completion
 * @return CH_FAILED if timeout occurs
 * @notapi
 */
static bool_t flash_wait_complete(const Flash25Config *cfg, systime_t timeout)
{
	systime_t now = chTimeNow();
	while (flash_is_busy(cfg)) {
		if (chTimeElapsedSince(now) >= timeout)
			return CH_FAILED; /* Timeout */

		chThdYield();
	}

	return CH_SUCCESS;
}

/**
 * @brief write status register (disable block protection)
 * @notapi
 */
static void flash_wrsr(const Flash25Config *cfg, uint8_t sr)
{
	uint8_t cmd[2];

	cmd[0] = CMD_EWSR;
	flash_transfer(cfg, cmd, 1, NULL, 0);

	cmd[0] = CMD_WRSR;
	cmd[1] = sr;
	flash_transfer(cfg, cmd, 2, NULL, 0);
}

/**
 * @brief read JDEC ID from device
 * @notapi
 */
static uint32_t flash_get_jdec_id(const Flash25Config *cfg)
{
	uint8_t cmd = CMD_JDEC_ID;
	uint8_t jdec[3];

	/* JDEC: 3 bytes */
	flash_transfer(cfg, &cmd, 1, jdec, sizeof(jdec));
	return (jdec[0] << 16) | (jdec[1] << 8) | jdec[2];
}

/**
 * @brief prepare command with address
 * Currently for SST25 with 24-bit addresses only.
 *
 * @notapi
 */
static void flash_prepare_cmd(uint8_t *buff, uint8_t cmd, uint32_t addr)
{
	buff[0] = cmd;
	buff[1] = (addr >> 16) & 0xff;
	buff[2] = (addr >> 8) & 0xff;
	buff[3] = addr & 0xff;
}

/**
 * @brief Normal read (F_clk < 25 MHz)
 * @notapi
 */
static void flash_read(const Flash25Config *cfg, uint32_t addr,
		uint8_t *buffer, uint32_t nbytes)
{
	uint8_t cmd[4];
	dbgPrint("addr: 0x%06x, nbytes: %d", addr, nbytes);
	flash_prepare_cmd(cmd, CMD_READ, addr);
	flash_transfer(cfg, cmd, sizeof(cmd), buffer, nbytes);
}

/**
 * @brief Fast read (F_clk < 80 MHz)
 * @notapi
 */
static void flash_fast_read(const Flash25Config *cfg, uint32_t addr,
		uint8_t *buffer, uint32_t nbytes)
{
	uint8_t cmd[5];
	dbgPrint("addr: 0x%06x, nbytes: %d", addr, nbytes);
	flash_prepare_cmd(cmd, CMD_FAST_READ, addr);
	cmd[4] = 0xa5; /* dummy byte */
	flash_transfer(cfg, cmd, sizeof(cmd), buffer, nbytes);
}

/**
 * @brief Set/Reset write lock
 * @notapi
 */
static void flash_wrlock(const Flash25Config *cfg, bool_t lock)
{
	uint8_t cmd = (lock)? CMD_WREN : CMD_WRDI;
	flash_transfer(cfg, &cmd, 1, NULL, 0);
}

/**
 * @brief Slow write (one byte per cycle)
 * @return CH_FAILED if timeout occurs
 * @notapi
 */
static bool_t flash_write_byte(const Flash25Config *cfg, uint32_t addr,
		const uint8_t *buffer, uint32_t nbytes)
{
	uint8_t cmd[5];
	bool_t ret;

	for (; nbytes > 0; nbytes--, buffer++, addr++) {
		/* skip bytes equal to erased state */
		if (*buffer == 0xff)
			continue;

		flash_prepare_cmd(cmd, CMD_BYTE_PROG, addr);
		cmd[4] = *buffer;

		flash_wrlock(cfg, false);
		flash_transfer(cfg, cmd, sizeof(cmd), NULL, 0);
		ret = flash_wait_complete(cfg, FLASH_TIMEOUT);
		flash_wrlock(cfg, true);

		if (ret == CH_FAILED)
			break;
	}

	return ret;
}

/**
 * @brief Fast write (word per cycle)
 * Based on sst25.c mtd driver from NuttX
 *
 * @return CH_FAILED if timeout occurs
 * @notapi
 */
static bool_t flash_write_word(const Flash25Config *cfg, uint32_t addr,
		const uint8_t *buff, uint32_t nbytes)
{
	uint32_t nwords = (nbytes + 1) / 2;
	uint8_t cmd[4];

	while (nwords > 0) {
		/* skip words equal to erased state */
		while (nwords > 0 && buff[0] == 0xff && buff[1] == 0xff) {
			nwords--;
			addr += 2;
			buff += 2;
		}

		if (nwords == 0)
			return CH_SUCCESS; /* all data written */

		flash_prepare_cmd(cmd, CMD_AAI_WORD_PROG, addr);
		flash_wrlock(cfg, false);

#if SPI_USE_MUTUAL_EXCLUSION
		spiAcquireBus(cfg->spip);
#endif

		spiStart(cfg->spip, cfg->spicfg);
		spiSelect(cfg->spip);
		spiSend(cfg->spip, sizeof(cmd), cmd);
		spiSend(cfg->spip, 2, buff);
		spiUnselect(cfg->spip);

#if SPI_USE_MUTUAL_EXCLUSION
		spiReleaseBus(cfg->spip);
#endif

		if (flash_wait_complete(cfg, FLASH_TIMEOUT) == CH_FAILED) {
			flash_wrlock(cfg, true);
			return CH_FAILED;
		}

		nwords--;
		addr += 2;
		buff += 2;

		/* write 16-bit cunks */
		while (nwords > 0 && (buff[0] != 0xff && buff[1] != 0xff)) {
#if SPI_USE_MUTUAL_EXCLUSION
			spiAcquireBus(cfg->spip);
#endif

			spiStart(cfg->spip, cfg->spicfg);
			spiSelect(cfg->spip);
			spiSend(cfg->spip, 1, cmd); /* CMD_AAI_WORD_PROG */
			spiSend(cfg->spip, 2, buff);
			spiUnselect(cfg->spip);

#if SPI_USE_MUTUAL_EXCLUSION
			spiReleaseBus(cfg->spip);
#endif

			if (flash_wait_complete(cfg, FLASH_TIMEOUT) == CH_FAILED) {
				flash_wrlock(cfg, true);
				return CH_FAILED;
			}

			nwords--;
			addr += 2;
			buff += 2;
		}

		flash_wrlock(cfg, true);
	}

	return CH_SUCCESS;
}

static bool_t flash_chip_erase(const Flash25Config *cfg)
{
	uint8_t cmd = CMD_CHIP_ERASE;
	bool_t ret;

	flash_wrlock(cfg, false);
	flash_transfer(cfg, &cmd, 1, NULL, 0);
	ret = flash_wait_complete(cfg, ERASE_TIMEOUT);
	flash_wrlock(cfg, true);
	return ret;
}

static bool_t flash_erase_block(const Flash25Config *cfg, uint32_t addr)
{
	uint8_t cmd[4];
	bool_t ret;

	flash_prepare_cmd(cmd, CMD_ERASE_4K, addr);
	flash_wrlock(cfg, false);
	flash_transfer(cfg, cmd, sizeof(cmd), NULL, 0);
	ret = flash_wait_complete(cfg, ERASE_TIMEOUT);
	flash_wrlock(cfg, true);
	return ret;
}

/*
 * VMT functions
 */

/**
 * @brief for unused fields of VMT
 * @notapi
 */
static bool_t f25_vmt_nop(void *instance __attribute__((unused)))
{
	return CH_SUCCESS;
}

/**
 * @brief probe flash chip
 * Select page/erase/size of chip
 * @api
 */
static bool_t f25_connect(Flash25Driver *inst)
{
	const struct flash_info *ptbl;

	inst->state = BLK_CONNECTING;
	inst->jdec_id = flash_get_jdec_id(inst->config);

	for (ptbl = flash_info_table;
			ptbl < (flash_info_table + ARRAY_SIZE(flash_info_table));
			ptbl++)
		if (ptbl->jdec_id == inst->jdec_id) {
			inst->state = BLK_ACTIVE;
			inst->page_size = ptbl->page_size;
			inst->erase_size = ptbl->erase_size;
			inst->nr_pages = ptbl->nr_pages;

			/* disable write protection BP[0..3] = 0 */
			//flash_wrsr(inst->config, 0);
			//flash_wrsr(inst->config, 0);
			return CH_SUCCESS;
		}

	inst->state = BLK_STOP;
	return CH_FAILED;
}

/**
 * @brief reads blocks from flash
 * @api
 */
static bool_t f25_read(Flash25Driver *inst, uint32_t startblk,
		uint8_t *buffer, uint32_t n)
{
	uint32_t addr = startblk * inst->page_size;
	uint32_t nbytes = n * inst->page_size;

	chDbgCheck(inst->state == BLK_ACTIVE, "f25_read()");

	flash_read(inst->config, addr, buffer, nbytes);
	//flash_fast_read(inst->config, addr, buffer, nbytes);
	return CH_SUCCESS;
}

/**
 * @brief writes blocks to flash
 * @api
 */
static bool_t f25_write(Flash25Driver *inst, uint32_t startblk,
		const uint8_t *buffer, uint32_t n)
{
	uint32_t addr = startblk * inst->page_size;
	uint32_t nbytes = n * inst->page_size;

	chDbgCheck(inst->state == BLK_ACTIVE, "f25_write()");

	//return flash_write_byte(inst->config, addr, buffer, nbytes);
	return flash_write_word(inst->config, addr, buffer, nbytes);
}

/**
 * @brief erase blocks on flash
 * If startblk is 0 and n more than chip capacity then erases whole chip.
 *
 * @param[in] startblk start block number
 * @param[in] n block count (must be equal to erase size, eg. for 4096 es, 256 ps -> n % 4096/256)
 * @api
 */
static bool_t f25_erase(Flash25Driver *inst, uint32_t startblk, uint32_t n)
{
	uint32_t addr;
	uint32_t nblocks;
	bool_t ret = CH_FAILED;

	chDbgCheck(inst->state == BLK_ACTIVE, "f25_erase()");

	if (startblk == 0 && n >= inst->nr_pages)
		return flash_chip_erase(inst->config);

	chDbgAssert((n % (inst->erase_size / inst->page_size)) == 0,
			"f25_erase()", "invalid size");

	addr = startblk * inst->page_size;
	nblocks = (n + 1) / (inst->erase_size / inst->page_size);
	for (; nblocks > 0; nblocks--, addr += inst->erase_size) {
		ret = flash_erase_block(inst->config, addr);
		if (ret == CH_FAILED)
			break;
	}

	return ret;
}

/**
 * @brief Get block device info (page size and noumber of pages)
 * @api
 */
static bool_t f25_get_info(Flash25Driver *inst, BlockDeviceInfo *bdip)
{
	if (inst->state != BLK_ACTIVE)
		return CH_FAILED;

	bdip->blk_size = inst->page_size;
	bdip->blk_num = inst->nr_pages;
	return CH_SUCCESS;
}

static const struct Flash25DriverVMT f25_vmt = {
	.is_inserted = f25_vmt_nop,
	.is_protected = f25_vmt_nop,
	.connect = (bool_t (*)(void*)) f25_connect,
	.disconnect = f25_vmt_nop,
	.read = (bool_t (*)(void*, uint32_t, uint8_t*, uint32_t)) f25_read,
	.write = (bool_t (*)(void*, uint32_t, const uint8_t*, uint32_t)) f25_write,
	.sync = f25_vmt_nop,
	.get_info = (bool_t (*)(void*, BlockDeviceInfo*)) f25_get_info,
	.erase = (bool_t (*)(void*, uint32_t, uint32_t)) f25_erase
};

/*
 * public interface
 */

/**
 * @brief Flash25 driver initialization.
 *
 * @init
 */
void f25Init(void)
{
}

/**
 * @brief Initializes an instance.
 *
 * @init
 */
void f25ObjectInit(Flash25Driver *flp)
{
	chDbgCheck(flp != NULL, "f25ObjectInit");

	flp->vmt = &f25_vmt;
	flp->config = NULL;
	flp->state = BLK_STOP;
	flp->jdec_id = 0;
	flp->page_size = 0;
	flp->erase_size = 0;
	flp->nr_pages = 0;
}

/**
 * @brief start flash device
 * @api
 */
void f25Start(Flash25Driver *flp, const Flash25Config *cfg)
{
	chDbgCheck((flp != NULL) && (cfg != NULL), "f25Start");
	chDbgAssert((flp->state == BLK_STOP) || (flp->state == BLK_ACTIVE),
			"f25Start()", "invalid state");

	flp->config = cfg;
	flp->state = BLK_ACTIVE;
}

/**
 * @brief stops device
 * @api
 */
void f25Stop(Flash25Driver *flp)
{
	chDbgCheck(flp != NULL, "f25Stop");
	chDbgAssert((flp->state == BLK_STOP) || (flp->state == BLK_ACTIVE),
			"f25Start()", "invalid state");

	spiStop(flp->config->spip);
	flp->state = BLK_STOP;
}

