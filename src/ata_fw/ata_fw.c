#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libatafw/common.h>
#include <libatafw/libatafw.h>
#include <libatafw/libatafw_err.h>
#include "ata_fw.h"

static struct ata_fw_context g_ata_fw_context;

static void reset_ata_fw_request_context(void)
{
	g_ata_fw_context.num_requests = 0;
	memset(g_ata_fw_context.requests, 0, sizeof(g_ata_fw_context.requests));
}

/* This function fills the CDB structure so that it would be suitable to pass it to the SG_IO subsystem for sending a single firmware chunk. 
	This function assumes that the parameters are correct (i.e., no NULL parameters and that all lengths provided to this function are aligned to sector size).
*/
static void fill_cdb(IN uint32_t offset, IN void *buffer, IN uint32_t buffer_size, OUT struct ata_cdb *cdb)
{
	uint16_t offset_in_sectors = offset / LIBATAFW_SECTOR_SIZE;
	uint16_t buffer_size_in_sectors = buffer_size / LIBATAFW_SECTOR_SIZE;

	cdb->operation_code = ATA_PASSTHROUGH_OPERATION_CODE;
	cdb->protocol = ATA_PROTOCOL_PIO_DATA_OUT;
	cdb->t_length = T_LENGTH_USE_SECTOR_COUNT_FIELD;
	cdb->t_dir = T_DIR_DIRECTION_OUT;
	cdb->byt_blok = BYT_BLOCK_USE_BLOCKS;
	cdb->feature = ATA_DNLD_SUB_CMD_DNLD_WITH_OFFSETS;
	cdb->block_count = buffer_size_in_sectors & 0xFF;
	cdb->lba_low = (buffer_size_in_sectors >> 8) & 0xFF;
	cdb->lba_mid = offset_in_sectors & 0xFF;
	cdb->lba_high = (offset_in_sectors >> 8) & 0xFF;
	cdb->command = ATA_CMD_DOWNLOAD_MICROCODE_PIO;
}

/* This function creates the actual IO request before it is passed to ioctl. Parameters are assumed to be valid. 
	It should be noted that cmdp and sbp fields of the filled struct must be released by the caller after the request is processed.
*/
static enum ata_fw_error fill_ata_download_request(IN uint32_t offset, IN void *buffer, IN uint32_t buffer_size, OUT sg_io_hdr_t *request)
{
	enum ata_fw_error status = ATA_FW_ERR_UNINITIALIZED;
	struct ata_cdb *request_cdb = NULL;
	void *request_sense_buffer = NULL;

	request_cdb = (struct ata_cdb *)calloc(sizeof(*request_cdb), 1);
	request_sense_buffer = calloc(SENSE_BUFFER_LENGTH, sizeof(uint8_t));

	if (NULL == request_cdb || NULL == request_sense_buffer) {
		status = ATA_FW_ERR_EXTERNAL_FUNCTION_FAILED;
		goto l_cleanup;
	}

	fill_cdb(offset, buffer, buffer_size, request_cdb);

	request->interface_id = SG_IO_INTERFACE_ID;
	request->cmdp = (unsigned char *)request_cdb;
	request->mx_sb_len = sizeof(request_sense_buffer);
	request->cmd_len = sizeof(*request_cdb);
	request->dxfer_direction = SG_DXFER_TO_DEV;
	request->dxferp = buffer;
	request->dxfer_len = buffer_size;
	request->sbp = request_sense_buffer;
	request->timeout = STANDARD_TIMEOUT_MS;

	/* Transfer ownership. */
	request_cdb = NULL;
	request_sense_buffer = NULL;

	status = ATA_FW_ERR_SUCCESS;
l_cleanup:

	/* On error, release the memory allocated inside the function to avoid memory leaks. */
	if (NULL != request_cdb) {
		free(request_cdb);
	}

	if (NULL != request_sense_buffer) {
		free(request_sense_buffer);
	}

	return status;
}

enum ata_fw_error libatafw__init(IN const char *device_path)
{
	enum ata_fw_error status = ATA_FW_ERR_UNINITIALIZED;
	int32_t device_fd = INVALID_FILE_DESCRIPTOR;

	if (NULL == device_path) {
		status = ATA_FW_ERR_NULL_PARAMETER;
		goto l_cleanup;
	}

	g_ata_fw_context.device_fd = INVALID_FILE_DESCRIPTOR;
	reset_ata_fw_request_context();

	device_fd = open(device_path, O_RDWR);
	if (INVALID_FILE_DESCRIPTOR == device_fd) {
		status = ATA_FW_ERR_EXTERNAL_FUNCTION_FAILED;
		goto l_cleanup;
	}

	g_ata_fw_context.device_fd = device_fd;
	/* Transfer ownership. */
	device_fd = INVALID_FILE_DESCRIPTOR;

	status = ATA_FW_ERR_SUCCESS;
l_cleanup:
	if (INVALID_FILE_DESCRIPTOR != device_fd) {
		close(device_fd);
	}

	return status;
}

enum ata_fw_error libatafw__enqueue_firmware_chunk(IN uint32_t offset, IN void *chunk_data, IN uint32_t chunk_size)
{
	enum ata_fw_error status = ATA_FW_ERR_UNINITIALIZED;
	uint16_t next_chunk_index = 0;
	sg_io_hdr_t *request_to_fill = NULL;

	if (NULL == chunk_data) {
		status = ATA_FW_ERR_NULL_PARAMETER;
		goto l_cleanup;
	}

	if ((0 != offset % LIBATAFW_SECTOR_SIZE) || (0 != chunk_size % LIBATAFW_SECTOR_SIZE)) {
		status = ATA_FW_ERR_INVALID_PARAMETER;
		goto l_cleanup;
	}

	next_chunk_index = g_ata_fw_context.num_requests;
	request_to_fill = &g_ata_fw_context.requests[next_chunk_index];

	status = fill_ata_download_request(offset, chunk_data, chunk_size, request_to_fill);
	if (ATA_FW_ERR_SUCCESS != status) {
		goto l_cleanup;
	}

	g_ata_fw_context.num_requests++;

	status = ATA_FW_ERR_SUCCESS;
l_cleanup:
	return status;
}

enum ata_fw_error libatafw__execute_requests(IN bool ignore_response_errors)
{
	enum ata_fw_error status = ATA_FW_ERR_UNINITIALIZED;
	uint16_t request_index = 0;
	uint16_t num_requests = g_ata_fw_context.num_requests;
	uint8_t request_status = 0;

	for (request_index = 0; request_index < num_requests; request_index++) {
		if (ioctl(g_ata_fw_context.device_fd, SG_IO, &g_ata_fw_context.requests[request_index])) {
			status = ATA_FW_ERR_EXTERNAL_FUNCTION_FAILED;
			goto l_cleanup;
		}

		request_status = g_ata_fw_context.requests[request_index].status;
		if (SCSI_STATUS_GOOD != request_status && !ignore_response_errors) {
			status = ATA_FW_ERR_RESPONSE_ERROR;
			goto l_cleanup;
		}
	}

	reset_ata_fw_request_context();

	status = ATA_FW_ERR_SUCCESS;
l_cleanup:
	return status;
}

void libatafw__deinit(void)
{
	reset_ata_fw_request_context();
	close(g_ata_fw_context.device_fd);
	g_ata_fw_context.device_fd = INVALID_FILE_DESCRIPTOR;
}
