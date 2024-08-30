#pragma once

#include <scsi/sg.h>
#include <libatafw/common.h>

/* SG_IO related defines. */
#define SENSE_BUFFER_LENGTH					(0x20)
#define SG_IO_INTERFACE_ID					('S')
#define ATA_PASSTHROUGH_OPERATION_CODE		(0xA1)
#define STANDARD_TIMEOUT_MS					(5000) /* 5 seconds. */

enum ata_protocol_value
{
	ATA_PROTOCOL_HARD_RESET = 0,
	ATA_PROTOCOL_SRST,
	ATA_PROTOCOL_NON_DATA = 3,
	ATA_PROTOCOL_PIO_DATA_IN,
	ATA_PROTOCOL_PIO_DATA_OUT,
	ATA_PROTOCOL_DMA,
	ATA_PROTOCOL_DMA_QUEUED,
	ATA_PROTOCOL_DEVICE_DIAGNOSTIC,
	ATA_PROTOCOL_DEVICE_RESET,
	ATA_PROTOCOL_UDMA_DATA_IN,
	ATA_PROTOCOL_UDMA_DATA_OUT,
	ATA_PROTOCOL_FPDMA,
	ATA_PROTOCOL_RETURN_RESPONSE_INFORMATION = 15,
};

enum t_length_value
{
	T_LENGTH_NO_DATA_TRANSFER = 0,
	T_LENGTH_USE_FEATURE_FIELD,
	T_LENGTH_USE_SECTOR_COUNT_FIELD,
	T_LENGTH_OTHER,
};

enum byt_blok_value
{
	BYT_BLOCK_USE_BYTES = 0,
	BYT_BLOCK_USE_BLOCKS,
};

enum t_dir_value
{
	T_DIR_DIRECTION_OUT = 0,
	T_DIR_DIRECTION_IN,
};

/* ATA standard related defines. */
#define STATUS_BIT_ABORT_MASK			(0x02)
enum ata_cmd
{
	ATA_CMD_DOWNLOAD_MICROCODE_PIO = 0x92,
};

enum ata_dnld_sub_cmd
{
	ATA_DNLD_SUB_CMD_DNLD_WITH_OFFSETS = 0x0E,
};

#pragma pack(push, 1)

struct ata_cdb
{
	uint8_t operation_code;
	uint8_t rsvd1 : 1;
	uint8_t protocol: 4;
	uint8_t multiple_count : 3;
	uint8_t t_length : 2;
	uint8_t byt_blok : 1;
	uint8_t t_dir : 1;
	uint8_t rsvd2 : 1;
	uint8_t ck_cond : 1;
	uint8_t off_line : 2;
	uint8_t feature;
	uint8_t block_count;
	uint8_t lba_low;
	uint8_t lba_mid;
	uint8_t lba_high;
	uint8_t device;
	uint8_t command;
	uint8_t rsvd3;
	uint8_t control;
};

#pragma pack(pop)

/* SCSI related defines. */
#define SCSI_STATUS_GOOD				(0)

#define MAX_FW_CHUNKS					(1024)

#define INVALID_FILE_DESCRIPTOR			(-1)

struct ata_fw_context
{
	int32_t device_fd;
	uint16_t num_requests;
	sg_io_hdr_t requests[MAX_FW_CHUNKS];
};
