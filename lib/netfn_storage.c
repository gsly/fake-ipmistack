/* Copyright (c) 2013, Zdenek Styblik
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the Zdenek Styblik.
 * 4. Neither the name of the Zdenek Styblik nor the
 *    names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ZDENEK STYBLIK ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ZDENEK STYBLIK BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "fake-ipmistack/fake-ipmistack.h"

#include <stdlib.h>
#include <time.h>

static uint8_t bmc_time[4];
static uint16_t sel_resrv_id = 0;

struct ipmi_sel_entry {
	uint16_t record_id;
	uint8_t is_free;
	uint8_t record_type;
	uint32_t timestamp;
	uint16_t generator_id;
	uint8_t event_msg_fmt_rev;
	uint8_t sensor_type;
	uint8_t sensor_number;
	uint8_t event_dir_or_type;
	uint8_t event_data1;
	uint8_t event_data2;
	uint8_t event_data3;
} ipmi_sel_entries[] = {
	{ 0x0, 0x0 },
	{ 0x1, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 },
	{ 0x2, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 },
	{ 0x3, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 },
	{ 0x4, 0x1, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 },
	{ 0xFFFF, 0x0 }
};

/* (31.6) Add SEL Entry */
int
sel_add_entry(struct dummy_rq *req, struct dummy_rs *rsp)
{
	uint8_t *data;
	uint8_t data_len = 2 * sizeof(uint8_t);
	uint8_t found = 0;
	uint16_t record_id = 0;
	int i = 0;

	if (req->msg.data_len != 16) {
		rsp->ccode = CC_DATA_LEN;
		return (-1);
	}
	for (record_id = 1; ipmi_sel_entries[record_id].record_id != 0xFFFF;
			record_id++) {
		if (ipmi_sel_entries[record_id].is_free == 0x1) {
			found = 1;
			break;
		}
	}
	if (found < 1) {
		/* FIXME - probably check and set overflow here. */
		rsp->ccode = CC_NO_SPACE;
		return (-1);
	}

	data = malloc(data_len);
	if (data == NULL) {
		rsp->ccode = CC_UNSPEC;
		perror("malloc fail");
		return (-1);
	}

	printf("[INFO] SEL Record ID: %" PRIu16 "\n", record_id);
	printf("[INFO] Data from client:\n");
	for (i = 0; i < 15; i++) {
		printf("[INFO] data[%i] = %" PRIu8 "\n", i, data[i]);
	}
	ipmi_sel_entries[record_id].is_free = 0x0;
	ipmi_sel_entries[record_id].record_type = data[2];
	if (data[2] < 0xE0) {
		ipmi_sel_entries[record_id].timestamp = (uint32_t)time(NULL);
	} else {
		ipmi_sel_entries[record_id].timestamp = data[6] << 32;
		ipmi_sel_entries[record_id].timestamp = data[5] << 16;
		ipmi_sel_entries[record_id].timestamp = data[4] << 8;
		ipmi_sel_entries[record_id].timestamp = data[3];
	}
	ipmi_sel_entries[record_id].generator_id = data[8] << 8;
	ipmi_sel_entries[record_id].generator_id |= data[7];
	/* FIXME - EvM Rev conversion from IPMIv1.0 to IPMIv1.5+, p457 */
	ipmi_sel_entries[record_id].event_msg_fmt_rev = data[9];
	ipmi_sel_entries[record_id].sensor_type = data[10];
	ipmi_sel_entries[record_id].sensor_number = data[11];
	ipmi_sel_entries[record_id].event_dir_or_type = data[12];
	ipmi_sel_entries[record_id].event_data1 = data[13];
	ipmi_sel_entries[record_id].event_data2 = data[14];
	ipmi_sel_entries[record_id].event_data3 = data[15];

	data[0] = record_id >> 0;
	data[1] = record_id >> 8;
	rsp->data = data;
	rsp->data_len = data_len;
	rsp->ccode = CC_OK;
	return 0;
}

/* (31.9) Clear SEL */
int
sel_clear(struct dummy_rq *req, struct dummy_rs *rsp)
{
# define SEL_CLR_COMPLETE 1
# define SEL_CLR_IN_PROGRESS 0
	uint8_t action;
	uint8_t *data;
	uint8_t data_len = 1 * sizeof(uint8_t);
	uint16_t record_id = 0;
	uint16_t resrv_id_rcv = 0xF;
	if (req->msg.data_len != 6) {
		rsp->ccode = CC_DATA_LEN;
		return (-1);
	}

	resrv_id_rcv = req->msg.data[1] << 8;
	resrv_id_rcv |= req->msg.data[0];

	printf("[INFO] SEL Reservation ID: %" PRIu16 "\n",
			sel_resrv_id);
	printf("[INFO] SEL Reservation ID CLI: %" PRIu16 "\n",
			resrv_id_rcv);
	printf("[INFO] SEL Request: %" PRIX8 ", %" PRIX8 ", %" PRIX8 "\n",
			req->msg.data[2], req->msg.data[3],
			req->msg.data[4]);
	printf("[INFO] SEL Action: %" PRIX8 "\n", req->msg.data[5]);

	if (resrv_id_rcv != sel_resrv_id) {
		printf("[ERROR] SEL Reservation ID mismatch.\n");
		rsp->ccode = CC_DATA_FIELD_INV;
		return (-1);
	} else if (req->msg.data[2] != 0x43
			|| req->msg.data[3] != 0x4C
			|| req->msg.data[4] != 0x52) {
		perror("[ERROR] Expected CLR.\n");
		rsp->ccode = CC_DATA_FIELD_INV;
		return (-1);
	} else if (req->msg.data[5] != 0x00
			&& req->msg.data[5] != 0xAA) {
		printf("[ERROR] Expected 0x00 or 0xAA.\n");
		rsp->ccode = CC_DATA_FIELD_INV;
		return (-1);
	}

	data = malloc(data_len);
	if (data == NULL) {
		rsp->ccode = CC_UNSPEC;
		perror("malloc fail");
		return (-1);
	}

	for (record_id = 1; ipmi_sel_entries[record_id].record_id != 0xFFFF;
			record_id++) {
		printf("[INFO] Clearing SEL Entry ID: %" PRIu16 "\n",
				record_id);
		ipmi_sel_entries[record_id].is_free = 0x1;
	}

	if (req->msg.data[5] == 0xAA) {
		data[0] = SEL_CLR_IN_PROGRESS;
	} else {
		data[0] = SEL_CLR_COMPLETE;
	}

	rsp->data = data;
	rsp->data_len = data_len;
	rsp->ccode = CC_OK;
	return 0;
}

/* (31.3) Get SEL Allocation Info */
int
sel_get_allocation_info(struct dummy_rq *req, struct dummy_rs *rsp)
{
	uint8_t *data;
	uint8_t data_len = 9;

	if (req->msg.data_len > 0) {
		rsp->ccode = CC_DATA_LEN;
		return (-1);
	}

	data = malloc(data_len);
	if (data == NULL) {
		rsp->ccode = CC_UNSPEC;
		perror("malloc fail");
		return (-1);
	}
	/* Number of possible allocs */
	data[0] = 0;
	data[1] = 0;
	/* Alloc unit size in bytes */
	data[2] = 0;
	data[3] = 0;
	/* Number of free allocs */
	data[4] = 0;
	data[5] = 0;
	/* Largest free block in alloc units */
	data[6] = 0;
	data[7] = 0;
	/* Max record size in alloc units */
	data[8] = 0;
	rsp->data = data;
	rsp->data_len = data_len;
	rsp->ccode = CC_OK;
	return 0;
}

/* (31.2) Get SEL Info */
int
sel_get_info(struct dummy_rq *req, struct dummy_rs *rsp)
{
	uint8_t *data;
	uint8_t data_len = 14 * sizeof(uint8_t);
	data = malloc(data_len);
	if (data == NULL) {
		rsp->ccode = CC_UNSPEC;
		perror("malloc fail");
		return (-1);
	}
	/* SEL Version */
	data[0] = 0x51;
	/* Num of Entries - LS, MS Byte */
	data[1] = 0;
	data[2] = 0;
	/* Free space in bytes - LS, MS */
	data[3] = 0x0;
	data[4] = 0x1;
	/* Most recent addition tstamp */
	data[5] = 0;
	data[6] = 0;
	data[7] = 0;
	data[8] = 0;
	/* Most recent erase tstamp */
	data[9] = 0;
	data[10] = 0;
	data[11] = 0;
	data[12] = 0;
	/* Op-support:
	 * - Overflow[7] - & 0xFF for on
	 * - Support[3:0] - delete, partial, reserve, get alloc
	 */
	data[13] = 0xF;
	rsp->data = data;
	rsp->data_len = data_len;
	rsp->ccode = CC_OK;
	return 0;
}

/* (31.4) Reserve SEL */
int
sel_get_reservation(struct dummy_rq *req, struct dummy_rs *rsp)
{
	uint8_t *data;
	uint8_t data_len = 2 * sizeof(uint8_t);
	data = malloc(data_len);
	if (data == NULL) {
		rsp->ccode = CC_UNSPEC;
		perror("malloc fail");
		return (-1);
	}
	sel_resrv_id = (uint16_t)rand();
	printf("[INFO] SEL Reservation ID: %i\n", sel_resrv_id);
	data[0] = sel_resrv_id >> 0;
	data[1] = sel_resrv_id >> 8;
	rsp->data = data;
	rsp->data_len = data_len;
	rsp->ccode = CC_OK;
	return 0;
}

/* (31.10) Get SEL Time */
int
sel_get_time(struct dummy_rq *req, struct dummy_rs *rsp)
{
	static char tbuf[40];
	uint8_t *data;
	uint8_t data_len = 4 * sizeof(uint8_t);
	data = malloc(data_len);
	if (data == NULL) {
		rsp->ccode = CC_UNSPEC;
		perror("malloc fail");
		return (-1);
	}
	data[0] = bmc_time[0];
	data[1] = bmc_time[1];
	data[2] = bmc_time[2];
	data[3] = bmc_time[3];
	rsp->data = data;
	rsp->data_len = data_len;
	rsp->ccode = CC_OK;

	strftime(tbuf, sizeof(tbuf), "%m/%d/%Y %H:%M:%S",
			gmtime((time_t *)data));
	printf("Time sent to client: %s\n", tbuf);
	return 0;
}

/* (31.11) Set SEL Time */
int
sel_set_time(struct dummy_rq *req, struct dummy_rs *rsp)
{
	static char tbuf[40];
	if (req->msg.data_len != 4) {
		rsp->ccode = CC_DATA_LEN;
		return (-1);
	}
	printf("[0]: '%i'\n", req->msg.data[0]);
	printf("[1]: '%i'\n", req->msg.data[1]);
	printf("[2]: '%i'\n", req->msg.data[2]);
	printf("[3]: '%i'\n", req->msg.data[3]);
	bmc_time[0] = req->msg.data[0];
	bmc_time[1] = req->msg.data[1];
	bmc_time[2] = req->msg.data[2];
	bmc_time[3] = req->msg.data[3];

	strftime(tbuf, sizeof(tbuf), "%m/%d/%Y %H:%M:%S",
			gmtime((time_t *)bmc_time));
	printf("Time received from client: %s\n", tbuf);
	return 0;
}

int
netfn_storage_main(struct dummy_rq *req, struct dummy_rs *rsp)
{
	int rc = 0;
	rsp->msg.netfn = req->msg.netfn + 1;
	rsp->msg.cmd = req->msg.cmd;
	rsp->msg.lun = req->msg.lun;
	rsp->ccode = CC_OK;
	rsp->data_len = 0;
	rsp->data = NULL;
	switch (req->msg.cmd) {
	case SEL_ADD_ENTRY:
		rc = sel_add_entry(req, rsp);
		break;
	case SEL_CLEAR:
		rc = sel_clear(req, rsp);
		break;
	case SEL_GET_ALLOCATION_INFO:
		rc = sel_get_allocation_info(req, rsp);
		break;
	case SEL_GET_INFO:
		rc = sel_get_info(req, rsp);
		break;
	case SEL_GET_RESERVATION:
		rc = sel_get_reservation(req, rsp);
		break;
	case SEL_GET_TIME:
		rc = sel_get_time(req, rsp);
		break;
	case SEL_SET_TIME:
		rc = sel_set_time(req, rsp);
		break;
	default:
		rsp->ccode = CC_CMD_INV;
		rc = (-1);
	}
	return rc;
}
