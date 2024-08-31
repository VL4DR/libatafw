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
#include <libatafw/debug.h>
#include "ata_fw.h"

static struct ata_fw_context g_ata_fw_context;

static void reset_ata_fw_request_context(void)
{
	g_ata_fw_context.num_requests = 0;
	g_ata_fw_context.num_dummy_buffers = 0;
	memset(g_ata_fw_context.requests, 0, sizeof(g_ata_fw_context.requests));
	memset(g_ata_fw_context.dummy_buffers, 0, sizeof(g_ata_fw_context.dummy_buffers));
}

static void release_ata_fw_requests_resources(void)
{
	sg_io_hdr_t *request = NULL;
	uint16_t num_requests = g_ata_fw_context.num_requests;
	uint16_t num_dummy_buffers = g_ata_fw_context.num_dummy_buffers;
	uint16_t i = 0;

	for (i = 0; i < num_requests; i++) {
		request = &g_ata_fw_context.requests[i];
		if (NULL != request->sbp) {
			free(request->sbp);
			request->sbp = NULL;
		}

		if (NULL != request->cmdp) {
			free(request->cmdp);
			request->cmdp = NULL;
		}
	}

	for (i = 0; i < num_dummy_buffers; i++) {
		if (NULL != g_ata_fw_context.dummy_buffers[i]) {
			free(g_ata_fw_context.dummy_buffers[i]);
			g_ata_fw_context.dummy_buffers[i] = NULL;
		}
	}
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
	request_sense_buffer = calloc(LIBATAFW_SENSE_BUFFER_LENGTH, sizeof(uint8_t));

	if (NULL == request_cdb || NULL == request_sense_buffer) {
		LIBATAFW_ERROR("Memory allocation failed for either the request CDB or the sense buffer!\n");
		status = ATA_FW_ERR_EXTERNAL_FUNCTION_FAILED;
		goto l_cleanup;
	}

	fill_cdb(offset, buffer, buffer_size, request_cdb);

	request->interface_id = SG_IO_INTERFACE_ID;
	request->cmdp = (unsigned char *)request_cdb;
	request->mx_sb_len = LIBATAFW_SENSE_BUFFER_LENGTH;
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
		LIBATAFW_ERROR("NULL Param!\n");
		status = ATA_FW_ERR_NULL_PARAMETER;
		goto l_cleanup;
	}

	g_ata_fw_context.device_fd = INVALID_FILE_DESCRIPTOR;
	reset_ata_fw_request_context();

	device_fd = open(device_path, O_RDWR);
	if (INVALID_FILE_DESCRIPTOR == device_fd) {
		LIBATAFW_ERROR("Device could not be opened!\n");
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

enum ata_fw_error libatafw__enqueue_firmware_chunk(IN uint32_t offset, IN OPTIONAL void *chunk_data, IN uint32_t chunk_size)
{
	enum ata_fw_error status = ATA_FW_ERR_UNINITIALIZED;
	uint16_t next_chunk_index = 0;
	sg_io_hdr_t *request_to_fill = NULL;
	void *eff_chunk_data = NULL;
	bool is_dummy_chunk = false;

	if (0 == chunk_size) {
		LIBATAFW_ERROR("Chunk size cannot be 0!\n");
		status = ATA_FW_ERR_INVALID_PARAMETER;
		goto l_cleanup;
	}

	if ((0 != offset % LIBATAFW_SECTOR_SIZE) || (0 != chunk_size % LIBATAFW_SECTOR_SIZE)) {
		LIBATAFW_ERROR("One of the sizes specified is not aligned to LIBATAFW_SECTOR_SIZE!\n");
		status = ATA_FW_ERR_INVALID_PARAMETER;
		goto l_cleanup;
	}

	eff_chunk_data = chunk_data;
	if (NULL == chunk_data) {
		eff_chunk_data = calloc(chunk_size, sizeof(uint8_t));
		if (NULL == eff_chunk_data) {
			LIBATAFW_ERROR("Allocating a dummy chunk failed!\n");
			status = ATA_FW_ERR_EXTERNAL_FUNCTION_FAILED;
			goto l_cleanup;
		}

		is_dummy_chunk = true;
	}

	next_chunk_index = g_ata_fw_context.num_requests;
	request_to_fill = &g_ata_fw_context.requests[next_chunk_index];

	status = fill_ata_download_request(offset, eff_chunk_data, chunk_size, request_to_fill);
	if (ATA_FW_ERR_SUCCESS != status) {
		goto l_cleanup;
	}

	if (is_dummy_chunk) {
		g_ata_fw_context.num_dummy_buffers++;
	}

	g_ata_fw_context.num_requests++;

	/* Transfer ownership. */
	eff_chunk_data = NULL;

	status = ATA_FW_ERR_SUCCESS;
l_cleanup:
	
	if (NULL != eff_chunk_data) {
		free(eff_chunk_data);
	}
	
	return status;
}

enum ata_fw_error libatafw__enqueue_multiple_firmware_chunks(IN struct ata_fw_chunk *chunks, IN uint16_t num_chunks)
{
	enum ata_fw_error status = ATA_FW_ERR_UNINITIALIZED;
	uint16_t i = 0;

	if (NULL == chunks || 0 == num_chunks) {
		LIBATAFW_ERROR("NULL Param!\n");
		status = ATA_FW_ERR_NULL_PARAMETER;
		goto l_cleanup;
	}

	for (i = 0; i < num_chunks; i++) {

		status = libatafw__enqueue_firmware_chunk(chunks[i].offset, chunks[i].chunk_data, chunks[i].chunk_size);
		if (ATA_FW_ERR_SUCCESS != status) {
			release_ata_fw_requests_resources();
			reset_ata_fw_request_context();
			goto l_cleanup;
		}
	}

	status = ATA_FW_ERR_SUCCESS;
l_cleanup:
	return status;
}

enum ata_fw_error libatafw__execute_requests(IN bool ignore_response_errors, OUT OPTIONAL uint8_t *scsi_status, OUT OPTIONAL void *sense_buffer)
{
	enum ata_fw_error status = ATA_FW_ERR_UNINITIALIZED;
	uint16_t request_index = 0;
	uint16_t num_requests = g_ata_fw_context.num_requests;
	uint8_t request_status = 0;
	sg_io_hdr_t *current_request = NULL;

	for (request_index = 0; request_index < num_requests; request_index++) {
		current_request = &g_ata_fw_context.requests[request_index];
		LIBATAFW_LOG("Executing request with index %d. Transfer size (bytes): %08x\n", request_index, current_request->dxfer_len);

		if (ioctl(g_ata_fw_context.device_fd, SG_IO, &g_ata_fw_context.requests[request_index])) {
			LIBATAFW_ERROR("ioctl failed!\n");
			status = ATA_FW_ERR_EXTERNAL_FUNCTION_FAILED;
			goto l_cleanup;
		}

		request_status = current_request->status;
		if (SCSI_STATUS_GOOD != request_status && !ignore_response_errors) {
			LIBATAFW_ERROR("SCSI status: %08x.\n", request_status);
			if (NULL != scsi_status) {
				*scsi_status = request_status;
			}

			if (NULL != sense_buffer) {
				memcpy(sense_buffer, current_request->sbp, LIBATAFW_SENSE_BUFFER_LENGTH);
			}

			status = ATA_FW_ERR_RESPONSE_ERROR;
			goto l_cleanup;
		}
	}

	LIBATAFW_LOG("All requests processed!\n");

	release_ata_fw_requests_resources();
	reset_ata_fw_request_context();

	status = ATA_FW_ERR_SUCCESS;
l_cleanup:
	return status;
}

void libatafw__deinit(void)
{
	release_ata_fw_requests_resources();
	reset_ata_fw_request_context();
	close(g_ata_fw_context.device_fd);
	g_ata_fw_context.device_fd = INVALID_FILE_DESCRIPTOR;
}
