#pragma once

#include <libatafw/common.h>
#include <libatafw/libatafw_err.h>

#define LIBATAFW_SECTOR_SIZE			(0x200)
#define LIBATAFW_SENSE_BUFFER_LENGTH	(0x20)

struct ata_fw_chunk
{
	uint32_t offset;
	void *chunk_data;
	uint32_t chunk_size;
};

/** @brief this function initializes the library and must be called only once.
 *  @param device_path - the path to the ATA device to popen.
 *  @return ata_fw_error - LIBATA_FW_ERR_SUCCESS on success, and an appropriate error code on error.
*/
enum ata_fw_error libatafw__init(IN const char *device_path);

/** @brief this function adds a new firmware chunk to the queue of pending FW download requests. Minimal sanity checks are performed on
 *  the requests.
 *  @param offset - the offset, in bytes, of the sequence of download requests. This parameter (divided by the sector size) corresponds
 *  to the value that will be written to the block offset field within the ATA request. This field must be aligned to `LIBATAFW_SECTOR_SIZE`.
 *  @param chunk_data - a pointer to the chunk data that should be downloaded.
 *  @param chunk_size - the size, in bytes, of the memory associcated with `chunk_data`. This parameter (divided by the sector size) corresponds\
 *  to the value that will be written to the block count field within the ATA request. This field must be aligned to `LIBATAFW_SECTOR_SIZE`.
 *  @return ata_fw_error - LIBATA_FW_ERR_SUCCESS on success, and an appropriate error code on error.
*/
enum ata_fw_error libatafw__enqueue_firmware_chunk(IN uint32_t offset, IN void *chunk_data, IN uint32_t chunk_size);

/** @brief this function adds multiple firmware chunks. This is a convenience function that allows performing multiple enqueues in a single call.
 *  This function makes sure that the chunk metadata is correct, and halts on the first invalid chunk. If that happens, then all accumulated chunks
 *  are released (whether they were enqueued using this function or using `libatafw__enqueue_firmware_chunk`).
 *  @param chunks - a pointer to an array of chunks.
 *  @param num_chunks - the number of chunks in `chunks`.
 *  @return ata_fw_error - LIBATA_FW_ERR_SUCCESS on success, and an appropriate error code on error.
*/
enum ata_fw_error libatafw__enqueue_multiple_firmware_chunks(IN struct ata_fw_chunk *chunks, IN uint16_t num_chunks);

/** @brief this function executes the accumulated requests inside the request queue. The requests are automatically deleted from the queue
 *  so that the queue is ready for a fresh sequence of requests.
 *  @param ignore_response_errors - if a response to one of the requests within the queue indicated failure, this parameter determines
 *  whether to ignore these errors and continue to the next requests. If an internal error occurs, on the other hand, the execution is halted.
 *  @param status - if `ignore_response_errors` is false, then upon an error, this parameter will be set to the SCSI status code. This parameter is optional,
 *  so NULL may be provided.
 *  @param sense_buffer - if `ignore_response_errors` is false, then upon an error, this parameter will be filled with the content of the sense buffer (if applicable).
 *  This parameter is optional, so NULL may be provided. If provided, it must be of size at least `LIBATAFW_SENSE_BUFFER_LENGTH` bytes.
 *  @return ata_fw_error - LIBATA_FW_ERR_SUCCESS on success, and an appropriate error code on error.
*/
enum ata_fw_error libatafw__execute_requests(IN bool ignore_response_errors, OUT OPTIONAL uint8_t *scsi_status, OUT OPTIONAL void *sense_buffer);

/** @brief this function deallocated the resources associated with the library. This should be called when the library it not to be used anymore.
*/
void libatafw__deinit(void);
