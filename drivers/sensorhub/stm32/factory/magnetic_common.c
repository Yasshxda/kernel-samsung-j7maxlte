/*
 *  Copyright (C) 2015, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */
#include "../ssp.h"

/*************************************************************************/
/* factory Sysfs                                                         */
/*************************************************************************/

#define YAS_ID		0
#define VENDOR_YAS		"YAMAHA"
#define CHIP_ID_YAS		"YAS539"

#define AKM_ID			1
#define VENDOR_AKM			"AKM"
#define CHIP_ID_AKM			"AK09916C"

#define GM_AKM_DATA_SPEC_MIN	-6500
#define GM_AKM_DATA_SPEC_MAX	6500

#define GM_YAS_DATA_SPEC_MIN	-6500
#define GM_YAS_DATA_SPEC_MAX	6500

#define GM_SELFTEST_X_SPEC_MIN	-200
#define GM_SELFTEST_X_SPEC_MAX	200
#define GM_SELFTEST_Y_SPEC_MIN	-200
#define GM_SELFTEST_Y_SPEC_MAX	200
#define GM_SELFTEST_Z_SPEC_MIN	-1000
#define GM_SELFTEST_Z_SPEC_MAX	-200

#define YAS_STATIC_ELLIPSOID_MATRIX 	{10000, 0, 0, 0, 10000, 0, 0, 0, 10000}
#define MAG_HW_OFFSET_FILE_PATH	"/efs/FactoryApp/hw_offset"

static int check_data_spec(struct ssp_data *data, int sensortype)
{
	int data_spec_max = 0;
	int data_spec_min = 0;
	if(data->mag_type == AKM_ID)
	{
	    data_spec_max = GM_AKM_DATA_SPEC_MAX;
	    data_spec_min = GM_AKM_DATA_SPEC_MIN;
	}
	else
	{
	    data_spec_max = GM_YAS_DATA_SPEC_MAX;
	    data_spec_min = GM_YAS_DATA_SPEC_MIN;
	}
	
	if ((data->buf[sensortype].x == 0) &&
		(data->buf[sensortype].y == 0) &&
		(data->buf[sensortype].z == 0))
		return FAIL;
	else if ((data->buf[sensortype].x > data_spec_max)
		|| (data->buf[sensortype].x < data_spec_min)
		|| (data->buf[sensortype].y > data_spec_max)
		|| (data->buf[sensortype].y < data_spec_min)
		|| (data->buf[sensortype].z > data_spec_max)
		|| (data->buf[sensortype].z < data_spec_min))
		return FAIL;
	else
		return SUCCESS;
}

/* AKM Functions */
static ssize_t magnetic_get_asa(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d,%d,%d\n", (s16)data->uFuseRomData[0],
		(s16)data->uFuseRomData[1], (s16)data->uFuseRomData[2]);
}

static ssize_t magnetic_get_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	bool bSuccess;
	struct ssp_data *data = dev_get_drvdata(dev);

	if ((data->uFuseRomData[0] == 0) ||
		(data->uFuseRomData[0] == 0xff) ||
		(data->uFuseRomData[1] == 0) ||
		(data->uFuseRomData[1] == 0xff) ||
		(data->uFuseRomData[2] == 0) ||
		(data->uFuseRomData[2] == 0xff))
		bSuccess = false;
	else
		bSuccess = true;

	return sprintf(buf, "%s,%u\n", (bSuccess ? "OK" : "NG"), bSuccess);
}

static ssize_t magnetic_check_cntl(struct device *dev,
		struct device_attribute *attr, char *strbuf)
{
	bool bSuccess = false;
	
	struct ssp_data *data = dev_get_drvdata(dev);

	char *chTempBuf = NULL;

	if (!data->uMagCntlRegData) {
		bSuccess = true;
	} else {
		int ret;
		struct ssp_msg *msg;
		
		pr_info("[SSP] %s - check cntl register before selftest",
			__func__);
		msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
		if (msg == NULL) {
			pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n",
				__func__);
			return -ENOMEM;
		}
		
		chTempBuf = kzalloc(22, GFP_KERNEL | GFP_DMA);
		
		msg->cmd = GEOMAGNETIC_FACTORY;
		msg->length = 22;
		msg->options = AP2HUB_READ;
		msg->buffer = chTempBuf;
		msg->free_buffer = 0;

		ret = ssp_spi_sync(data, msg, 1000);

		if (ret != SUCCESS) {
			pr_err("[SSP] %s - spi sync failed due to Timeout!! %d\n",
					__func__, ret);
		}

		data->uMagCntlRegData = chTempBuf[21];
		bSuccess = !data->uMagCntlRegData;
	}

	pr_info("[SSP] %s - CTRL : 0x%x\n", __func__,
				data->uMagCntlRegData);

	data->uMagCntlRegData = 1;	/* reset the value */

	kfree(chTempBuf);
	return sprintf(strbuf, "%s,%d,%d,%d\n",
		(bSuccess ? "OK" : "NG"), (bSuccess ? 1 : 0), 0, 0);
}


static ssize_t magnetic_get_selftest_akm(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	s8 iResult[4] = {-1, -1, -1, -1};
	char bufAdc[4] = {0, };
	s16 iSF_X = 0, iSF_Y = 0, iSF_Z = 0;
	s16 iADC_X = 0, iADC_Y = 0, iADC_Z = 0;
	s32 dMsDelay = 20;
	int ret = 0, iSpecOutRetries = 0;
	struct ssp_data *data = dev_get_drvdata(dev);
	struct ssp_msg *msg;
	
	char *bufSelftset = NULL;
		
	pr_info("[SSP] %s in\n", __func__);

	/* STATUS AK09916C doesn't need FuseRomdata more*/
	iResult[0] = 0;

	bufSelftset = kzalloc(22, GFP_KERNEL | GFP_DMA);
Retry_selftest:
	msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n",
			__func__);
		goto exit;
	}

	msg->cmd = GEOMAGNETIC_FACTORY;
	msg->length = 22;
	msg->options = AP2HUB_READ;
	msg->buffer = bufSelftset;
	msg->free_buffer = 0;

	ret = ssp_spi_sync(data, msg, 1000);
	if (ret != SUCCESS) {
		pr_err("[SSP] %s - Magnetic Selftest Timeout!! %d\n",
			__func__, ret);
		goto exit;
	}

	/* read 6bytes data registers */
	iSF_X = (s16)((bufSelftset[13] << 8) + bufSelftset[14]);
	iSF_Y = (s16)((bufSelftset[15] << 8) + bufSelftset[16]);
	iSF_Z = (s16)((bufSelftset[17] << 8) + bufSelftset[18]);

	/* DAC (store Cntl Register value to check power down) */
	iResult[2] = bufSelftset[21];

	iSF_X = (s16)(((iSF_X * data->uFuseRomData[0]) >> 7) + iSF_X);
	iSF_Y = (s16)(((iSF_Y * data->uFuseRomData[1]) >> 7) + iSF_Y);
	iSF_Z = (s16)(((iSF_Z * data->uFuseRomData[2]) >> 7) + iSF_Z);

	pr_info("[SSP] %s: self test x = %d, y = %d, z = %d\n",
		__func__, iSF_X, iSF_Y, iSF_Z);

	if ((iSF_X >= GM_SELFTEST_X_SPEC_MIN)
		&& (iSF_X <= GM_SELFTEST_X_SPEC_MAX))
		pr_info("[SSP] x passed self test, expect -200<=x<=200\n");
	else
		pr_info("[SSP] x failed self test, expect -200<=x<=200\n");
	if ((iSF_Y >= GM_SELFTEST_Y_SPEC_MIN)
		&& (iSF_Y <= GM_SELFTEST_Y_SPEC_MAX))
		pr_info("[SSP] y passed self test, expect -200<=y<=200\n");
	else
		pr_info("[SSP] y failed self test, expect -200<=y<=200\n");
	if ((iSF_Z >= GM_SELFTEST_Z_SPEC_MIN)
		&& (iSF_Z <= GM_SELFTEST_Z_SPEC_MAX))
		pr_info("[SSP] z passed self test, expect -1000<=z<=-200\n");
	else
		pr_info("[SSP] z failed self test, expect -1000<=z<=-200\n");

	/* SELFTEST */
	if ((iSF_X >= GM_SELFTEST_X_SPEC_MIN)
		&& (iSF_X <= GM_SELFTEST_X_SPEC_MAX)
		&& (iSF_Y >= GM_SELFTEST_Y_SPEC_MIN)
		&& (iSF_Y <= GM_SELFTEST_Y_SPEC_MAX)
		&& (iSF_Z >= GM_SELFTEST_Z_SPEC_MIN)
		&& (iSF_Z <= GM_SELFTEST_Z_SPEC_MAX))
		iResult[1] = 0;

	if ((iResult[1] == -1) && (iSpecOutRetries++ < 5)) {
		pr_err("[SSP] %s, selftest spec out. Retry = %d", __func__,
			iSpecOutRetries);
		goto Retry_selftest;
	}

	iSpecOutRetries = 10;

	/* ADC */
	memcpy(&bufAdc[0], &dMsDelay, 4);

	data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x = 0;
	data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y = 0;
	data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z = 0;

	if (!(atomic64_read(&data->aSensorEnable) & (1 << SENSOR_TYPE_GEOMAGNETIC_POWER)))
		send_instruction(data, ADD_SENSOR, SENSOR_TYPE_GEOMAGNETIC_POWER,
			bufAdc, 4);

	do {
		msleep(60);
		if (check_data_spec(data, SENSOR_TYPE_GEOMAGNETIC_POWER) == SUCCESS)
			break;
	} while (--iSpecOutRetries);

	if (iSpecOutRetries > 0)
		iResult[3] = 0;

	iADC_X = data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x;
	iADC_Y = data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y;
	iADC_Z = data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z;

	if (!(atomic64_read(&data->aSensorEnable) & (1 << SENSOR_TYPE_GEOMAGNETIC_POWER)))
		send_instruction(data, REMOVE_SENSOR, SENSOR_TYPE_GEOMAGNETIC_POWER,
			bufAdc, 4);

	pr_info("[SSP] %s -adc, x = %d, y = %d, z = %d, retry = %d\n",
		__func__, iADC_X, iADC_Y, iADC_Z, iSpecOutRetries);

exit:
	pr_info("[SSP] %s out. Result = %d %d %d %d\n",
		__func__, iResult[0], iResult[1], iResult[2], iResult[3]);

	kfree(bufSelftset);

	return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
		iResult[0], iResult[1], iSF_X, iSF_Y, iSF_Z,
		iResult[2], iResult[3], iADC_X, iADC_Y, iADC_Z);
}

static int set_pdc_matrix(struct ssp_data *data)
{
	int ret = 0;
	struct ssp_msg *msg;

	if (!(data->uSensorState & 0x04)) {
		pr_info("[SSP] %s - Skip this function!!!"\
			", magnetic sensor is not connected(0x%llx)\n",
			__func__, data->uSensorState);
		return ret;
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp msg\n",
			__func__);
		return -ENOMEM;
	}

	msg->cmd = MSG2SSP_AP_SET_MAGNETIC_STATIC_MATRIX;
	msg->length = sizeof(data->pdc_matrix);
	msg->options = AP2HUB_WRITE;

#if 1
	msg->buffer = kzalloc(sizeof(data->pdc_matrix), GFP_KERNEL | GFP_DMA);
	if (msg->buffer == NULL) {
		pr_err("[SSP] %s, failed to alloc memory\n", __func__);
		kfree(msg);
		return -ENOMEM;
	}

	msg->free_buffer = 1;
	memcpy(msg->buffer, data->pdc_matrix, sizeof(data->pdc_matrix));
#else
	msg->buffer = matrix;
	msg->free_buffer = 0;
	memcpy(msg->buffer, data->pdc_matrix, sizeof(data->pdc_matrix));
#endif

	ret = ssp_spi_async(data, msg);
	if (ret != SUCCESS) {
		pr_err("[SSP] %s - i2c fail %d\n", __func__, ret);
		ret = ERROR;
	}

	pr_info("[SSP] %s: finished\n", __func__);

	return ret;
}

int get_fuserom_data(struct ssp_data *data)
{
	int ret = 0;
	
	char *buffer = NULL;
	
	struct ssp_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n",
			__func__);
		return -ENOMEM;
	}

	buffer = kzalloc(3, GFP_KERNEL | GFP_DMA);
	
	msg->cmd = MSG2SSP_AP_FUSEROM;
	msg->length = 3;
	msg->options = AP2HUB_READ;
	msg->buffer = buffer;
	msg->free_buffer = 0;

	ret = ssp_spi_sync(data, msg, 1000);

	if (ret) {
		data->uFuseRomData[0] = buffer[0];
		data->uFuseRomData[1] = buffer[1];
		data->uFuseRomData[2] = buffer[2];
	} else {
		data->uFuseRomData[0] = 0;
		data->uFuseRomData[1] = 0;
		data->uFuseRomData[2] = 0;
		kfree(buffer);
		return FAIL;
	}

	pr_info("[SSP] FUSE ROM Data %d , %d, %d\n", data->uFuseRomData[0],
			data->uFuseRomData[1], data->uFuseRomData[2]);

	kfree(buffer);
	return SUCCESS;
}

static ssize_t magnetic_logging_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = 0;
	int logging_data[8] = {0, };
	struct ssp_data *data = dev_get_drvdata(dev);
	struct ssp_msg *msg;

	char *buffer = NULL;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n",
			__func__);
		goto exit;
	}

	buffer = kzalloc(21, GFP_KERNEL | GFP_DMA);

	msg->cmd = MSG2SSP_AP_GEOMAG_LOGGING;
	msg->length = 21;
	msg->options = AP2HUB_READ;
	msg->buffer = buffer;
	msg->free_buffer = 0;

	ret = ssp_spi_sync(data, msg, 1000);
	if (ret != SUCCESS) {
		pr_err("[SSP] %s - Magnetic logging data Timeout!! %d\n",
			__func__, ret);
		goto exit;
	}

	logging_data[0] = buffer[0];	/* ST1 Reg */
	logging_data[1] = (short)((buffer[3] << 8) + buffer[2]);
	logging_data[2] = (short)((buffer[5] << 8) + buffer[4]);
	logging_data[3] = (short)((buffer[7] << 8) + buffer[6]);
	logging_data[4] = buffer[1];	/* ST2 Reg */
	logging_data[5] = (short)((buffer[9] << 8) + buffer[8]);
	logging_data[6] = (short)((buffer[11] << 8) + buffer[10]);
	logging_data[7] = (short)((buffer[13] << 8) + buffer[12]);

	kfree(buffer);
	return snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			logging_data[0], logging_data[1],
			logging_data[2], logging_data[3],
			logging_data[4], logging_data[5],
			logging_data[6], logging_data[7],
			data->uFuseRomData[0], data->uFuseRomData[1],
			data->uFuseRomData[2]);
exit:
	kfree(buffer);
	return snprintf(buf, PAGE_SIZE, "-1,0,0,0,0,0,0,0,0,0,0\n");
}

/* YAS function*/
static ssize_t magnetic_get_selftest_yas(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int iRet = 0;
	s8 id = 0, x = 0, y1 = 0, y2 = 0, dir = 0;
	s16 sx = 0, sy1 = 0, sy2 = 0, ohx = 0, ohy = 0, ohz = 0;
	s8 err[7] = {-1, };
	struct ssp_data *data = dev_get_drvdata(dev);
	
	char *chTempBuf = NULL;

	struct ssp_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
		goto exit;
	}

	chTempBuf = kzalloc(24, GFP_KERNEL | GFP_DMA);
	
	msg->cmd = GEOMAGNETIC_FACTORY;
	msg->length = 24;
	msg->options = AP2HUB_READ;
	msg->buffer = chTempBuf;
	msg->free_buffer = 0;

	iRet = ssp_spi_sync(data, msg, 1000);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - Magnetic Selftest Timeout!! %d\n", __func__, iRet);
		goto exit;
	}

	id = (s8)(chTempBuf[0]);
	err[0] = (s8)(chTempBuf[1]);
	err[1] = (s8)(chTempBuf[2]);
	err[2] = (s8)(chTempBuf[3]);
	x = (s8)(chTempBuf[4]);
	y1 = (s8)(chTempBuf[5]);
	y2 = (s8)(chTempBuf[6]);
	err[3] = (s8)(chTempBuf[7]);
	dir = (s8)(chTempBuf[8]);
	err[4] = (s8)(chTempBuf[9]);
	ohx = (s16)((chTempBuf[10] << 8) + chTempBuf[11]);
	ohy = (s16)((chTempBuf[12] << 8) + chTempBuf[13]);
	ohz = (s16)((chTempBuf[14] << 8) + chTempBuf[15]);
	err[6] = (s8)(chTempBuf[16]);
	sx = (s16)((chTempBuf[17] << 8) + chTempBuf[18]);
	sy1 = (s16)((chTempBuf[19] << 8) + chTempBuf[20]);
	sy2 = (s16)((chTempBuf[21] << 8) + chTempBuf[22]);
	err[5] = (s8)(chTempBuf[23]);

	if (unlikely(id != 0x8))
		err[0] = -1;
	if (unlikely(x < -30 || x > 30))
		err[3] = -1;
	if (unlikely(y1 < -30 || y1 > 30))
		err[3] = -1;
	if (unlikely(y2 < -30 || y2 > 30))
		err[3] = -1;
	if (unlikely(sx < 16544 || sx > 17024))
		err[5] = -1;
	if (unlikely(sy1 < 16517 || sy1 > 17184))
		err[5] = -1;
	if (unlikely(sy2 < 15584 || sy2 > 16251))
		err[5] = -1;
	if (unlikely(ohx < -1000 || ohx > 1000))
		err[6] = -1;
	if (unlikely(ohy < -1000 || ohy > 1000))
		err[6] = -1;
	if (unlikely(ohz < -1000 || ohz > 1000))
		err[6] = -1;

	pr_info("[SSP] %s\n"
		"[SSP] Test1 - err = %d, id = %d\n"
		"[SSP] Test3 - err = %d\n"
		"[SSP] Test4 - err = %d, offset = %d,%d,%d\n"
		"[SSP] Test5 - err = %d, direction = %d\n"
		"[SSP] Test6 - err = %d, sensitivity = %d,%d,%d\n"
		"[SSP] Test7 - err = %d, offset = %d,%d,%d\n"
		"[SSP] Test2 - err = %d\n",
		__func__, err[0], id, err[2], err[3], x, y1, y2, err[4], dir,
		err[5], sx, sy1, sy2, err[6], ohx, ohy, ohz, err[1]);

exit:
	kfree(chTempBuf);
	return sprintf(buf,
			"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			err[0], id, err[2], err[3], x, y1, y2, err[4], dir,
			err[5], sx, sy1, sy2, err[6], ohx, ohy, ohz, err[1]);
}

int mag_open_hwoffset(struct ssp_data *data)
{
	int iRet = 0;
	mm_segment_t old_fs;
	struct file *cal_filp = NULL;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(MAG_HW_OFFSET_FILE_PATH, O_RDONLY, 0);
	if (IS_ERR(cal_filp)) {
		pr_err("[SSP] %s: filp_open failed\n", __func__);
		set_fs(old_fs);
		iRet = PTR_ERR(cal_filp);

		data->magoffset.x = 0;
		data->magoffset.y = 0;
		data->magoffset.z = 0;

		return iRet;
	}

	iRet = vfs_read(cal_filp, (char *)&data->magoffset,
		3 * sizeof(char), &cal_filp->f_pos);
	if (iRet != 3 * sizeof(char)) {
		pr_err("[SSP] %s: filp_open failed\n", __func__);
		iRet = -EIO;
	}

	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	ssp_dbg("[SSP]: %s: %d, %d, %d\n", __func__,
		(s8)data->magoffset.x,
		(s8)data->magoffset.y,
		(s8)data->magoffset.z);

	if ((data->magoffset.x == 0) && (data->magoffset.y == 0)
		&& (data->magoffset.z == 0))
		return ERROR;

	return iRet;
}

int get_hw_offset(struct ssp_data *data)
{
	int iRet = 0;
	char *buffer = NULL;
	

	struct ssp_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
		return -ENOMEM;
	}

	buffer = kzalloc(3, GFP_KERNEL | GFP_DMA);
	
	msg->cmd = MSG2SSP_AP_GET_MAGNETIC_HWOFFSET;
	msg->length = 3;
	msg->options = AP2HUB_READ;
	msg->buffer = buffer;
	msg->free_buffer = 0;

	data->magoffset.x = 0;
	data->magoffset.y = 0;
	data->magoffset.z = 0;

	iRet = ssp_spi_sync(data, msg, 1000);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - i2c fail %d\n", __func__, iRet);
		iRet = ERROR;
	}

	data->magoffset.x = buffer[0];
	data->magoffset.y = buffer[1];
	data->magoffset.z = buffer[2];

	pr_info("[SSP]: %s: x: %d, y: %d, z: %d\n", __func__,
		(s8)data->magoffset.x,
		(s8)data->magoffset.y,
		(s8)data->magoffset.z);
		
	kfree(buffer);
	return iRet;
}

int mag_store_hwoffset(struct ssp_data *data)
{
	struct file *cal_filp = NULL;
	mm_segment_t old_fs;

	if (get_hw_offset(data) < 0) {
		pr_err("[SSP]: %s - get_hw_offset failed\n", __func__);
		return ERROR;
	} else {
		int iRet = 0;
	
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		cal_filp = filp_open(MAG_HW_OFFSET_FILE_PATH,
			O_CREAT | O_TRUNC | O_WRONLY, 0660);
		if (IS_ERR(cal_filp)) {
			pr_err("[SSP]: %s - Can't open hw_offset file\n",
				__func__);
			set_fs(old_fs);
			iRet = PTR_ERR(cal_filp);
			return iRet;
		}
		iRet = vfs_write(cal_filp,
			(char *)&data->magoffset,
			3 * sizeof(char), &cal_filp->f_pos);
		if (iRet != 3 * sizeof(char)) {
			pr_err("[SSP]: %s - Can't write the hw_offset"
				" to file\n", __func__);
			iRet = -EIO;
		}
		filp_close(cal_filp, current->files);
		set_fs(old_fs);
		return iRet;
	}
}

int set_hw_offset(struct ssp_data *data)
{
	int iRet = 0;
	struct ssp_msg *msg;

	if (!(data->uSensorState & 0x04)) {
		pr_info("[SSP]: %s - Skip this function!!!"\
			", magnetic sensor is not connected(0x%llx)\n",
			__func__, data->uSensorState);
		return iRet;
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
		return -ENOMEM;
	}
	msg->cmd = MSG2SSP_AP_SET_MAGNETIC_HWOFFSET;
	msg->length = 3;
	msg->options = AP2HUB_WRITE;
	msg->buffer = (char*) kzalloc(3, GFP_KERNEL | GFP_DMA);
	msg->free_buffer = 1;

	msg->buffer[0] = data->magoffset.x;
	msg->buffer[1] = data->magoffset.y;
	msg->buffer[2] = data->magoffset.z;

	iRet = ssp_spi_async(data, msg);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - i2c fail %d\n", __func__, iRet);
		iRet = ERROR;
	}

	pr_info("[SSP]: %s: x: %d, y: %d, z: %d\n", __func__,
		(s8)data->magoffset.x, (s8)data->magoffset.y, (s8)data->magoffset.z);
	return iRet;
}

int set_static_matrix(struct ssp_data *data)
{
	int iRet = SUCCESS;
	struct ssp_msg *msg;
	s16 static_matrix[9] = YAS_STATIC_ELLIPSOID_MATRIX;

	if (!(data->uSensorState & 0x04)) {
		pr_info("[SSP]: %s - Skip this function!!!"\
			", magnetic sensor is not connected(0x%llx)\n",
			__func__, data->uSensorState);
		return iRet;
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL | GFP_DMA);
	if (msg == NULL) {
		pr_err("[SSP] %s, failed to alloc memory for ssp_msg\n", __func__);
		return -ENOMEM;
	}
	msg->cmd = MSG2SSP_AP_SET_MAGNETIC_STATIC_MATRIX;
	msg->length = 18;
	msg->options = AP2HUB_WRITE;
	msg->buffer = (char*) kzalloc(18, GFP_KERNEL | GFP_DMA);

	msg->free_buffer = 1;
	if (data->static_matrix == NULL)
		memcpy(msg->buffer, static_matrix, 18);
	else
		memcpy(msg->buffer, data->static_matrix, 18);

	iRet = ssp_spi_async(data, msg);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - i2c fail %d\n", __func__, iRet);
		iRet = ERROR;
	}
	pr_info("[SSP]: %s: finished \n", __func__);

	return iRet;
}

static ssize_t hw_offset_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	mag_open_hwoffset(data);

	pr_info("[SSP] %s: %d %d %d\n", __func__,
		(s8)data->magoffset.x,
		(s8)data->magoffset.y,
		(s8)data->magoffset.z);

	return sprintf(buf, "%d %d %d\n",
		(s8)data->magoffset.x,
		(s8)data->magoffset.y,
		(s8)data->magoffset.z);
}

static ssize_t matrix_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);	
	if(data->mag_type == AKM_ID)
	{
		return sprintf(buf,
			"%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
			data->pdc_matrix[0], data->pdc_matrix[1], data->pdc_matrix[2], data->pdc_matrix[3], data->pdc_matrix[4], 
			data->pdc_matrix[5], data->pdc_matrix[6], data->pdc_matrix[7], data->pdc_matrix[8], data->pdc_matrix[9], 
			data->pdc_matrix[10], data->pdc_matrix[11], data->pdc_matrix[12], data->pdc_matrix[13], data->pdc_matrix[14], 
			data->pdc_matrix[15], data->pdc_matrix[16], data->pdc_matrix[17], data->pdc_matrix[18], data->pdc_matrix[19], 
			data->pdc_matrix[20], data->pdc_matrix[21], data->pdc_matrix[22], data->pdc_matrix[23], data->pdc_matrix[24], 
			data->pdc_matrix[25], data->pdc_matrix[26]);
	}
	else
	{
		return sprintf(buf,
			"%d %d %d %d %d %d %d %d %d\n", data->static_matrix[0], data->static_matrix[1], data->static_matrix[2]
			, data->static_matrix[3], data->static_matrix[4], data->static_matrix[5]
			, data->static_matrix[6], data->static_matrix[7], data->static_matrix[8]);
	}
}

static ssize_t matrix_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct ssp_data *data = dev_get_drvdata(dev);	
 	int iRet;
 	int i;
	char* token;
	char* str;
	str = (char*)buf;

 	if(data->mag_type == AKM_ID)
 	{
		u8 val[PDC_SIZE]={0,};
		for(i=0;i<PDC_SIZE;i++)
	 	{
			token = strsep(&str, " \n");
			if(token == NULL)
			{
				pr_err("[SSP] %s : too few arguments (%d needed)",__func__,PDC_SIZE);
	 			return -EINVAL;
			}

			iRet = kstrtou8(token, 10, &val[i]);
			if (iRet<0) {
	 			pr_err("[SSP] %s : kstros16 error %d",__func__,iRet);
	 			return iRet;
	 		}
	 	}		
		
		for(i=0 ;i<PDC_SIZE;i++)
			data->pdc_matrix[i] = val[i];

		pr_info("[SSP] %s : %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", 
			__func__, data->pdc_matrix[0], data->pdc_matrix[1], data->pdc_matrix[2], data->pdc_matrix[3], data->pdc_matrix[4], 
			data->pdc_matrix[5], data->pdc_matrix[6], data->pdc_matrix[7], data->pdc_matrix[8], data->pdc_matrix[9], 
			data->pdc_matrix[10], data->pdc_matrix[11], data->pdc_matrix[12], data->pdc_matrix[13], data->pdc_matrix[14], 
			data->pdc_matrix[15], data->pdc_matrix[16], data->pdc_matrix[17], data->pdc_matrix[18], data->pdc_matrix[19], 
			data->pdc_matrix[20], data->pdc_matrix[21], data->pdc_matrix[22], data->pdc_matrix[23], data->pdc_matrix[24], 
			data->pdc_matrix[25], data->pdc_matrix[26]);
		set_pdc_matrix(data);
 	}
	else
	{
		s16 val[9]={0,};
		for(i=0;i<9;i++)
	 	{
			token = strsep(&str, " \n");
			if(token == NULL)
			{
				pr_err("[SSP] %s : too few arguments (9 needed)",__func__);
	 			return -EINVAL;
			}

			iRet = kstrtos16(token, 10, &val[i]);
			if (iRet<0) {
	 			pr_err("[SSP] %s : kstros16 error %d",__func__,iRet);
	 			return iRet;
	 		}
	 	}		
		
		for(i=0 ;i<9;i++)
			data->static_matrix[i] = val[i];

		pr_info("[SSP] %s : %d %d %d %d %d %d %d %d %d\n",__func__,data->static_matrix[0], data->static_matrix[1], data->static_matrix[2]
			, data->static_matrix[3], data->static_matrix[4], data->static_matrix[5]
			, data->static_matrix[6], data->static_matrix[7], data->static_matrix[8]);
		
		set_static_matrix(data);
	}

	return size;
}

/* common functions */

static ssize_t magnetic_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	if(data->mag_type == AKM_ID)
		return sprintf(buf, "%s\n", VENDOR_AKM);
	else
		return sprintf(buf, "%s\n", VENDOR_YAS);
}

static ssize_t magnetic_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	
	if(data->mag_type == AKM_ID)
		return sprintf(buf, "%s\n", CHIP_ID_AKM);
	else
	    return sprintf(buf, "%s\n", CHIP_ID_YAS);
}

static ssize_t raw_data_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);

	pr_info("[SSP] %s - %d,%d,%d\n", __func__,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z);

	if (data->bGeomagneticRawEnabled == false) {
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x = -1;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y = -1;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z = -1;
	}

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y,
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z);
}

static ssize_t raw_data_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	char chTempbuf[4] = { 0 };
	int ret;
	int64_t dEnable;
	struct ssp_data *data = dev_get_drvdata(dev);
	s32 dMsDelay = 20;
	memcpy(&chTempbuf[0], &dMsDelay, 4);

	ret = kstrtoll(buf, 10, &dEnable);
	if (ret < 0)
		return ret;

	if (dEnable) {
		int retries = 50;
		
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].x = 0;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].y = 0;
		data->buf[SENSOR_TYPE_GEOMAGNETIC_POWER].z = 0;

		send_instruction(data, ADD_SENSOR, SENSOR_TYPE_GEOMAGNETIC_POWER,
			chTempbuf, 4);

		do {
			msleep(20);
			if (check_data_spec(data, SENSOR_TYPE_GEOMAGNETIC_POWER) == SUCCESS)
				break;
		} while (--retries);

		if (retries > 0)
		{
			pr_info("[SSP] %s - success, %d\n", __func__, retries);
            data->bGeomagneticRawEnabled = true;
		}
		else
		{
			pr_err("[SSP] %s - wait timeout, %d\n", __func__,
				retries);
            data->bGeomagneticRawEnabled = false;
		}


	} else {
		send_instruction(data, REMOVE_SENSOR, SENSOR_TYPE_GEOMAGNETIC_POWER,
			chTempbuf, 4);
		data->bGeomagneticRawEnabled = false;
	}

	return size;
}

static ssize_t adc_data_read(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	bool bSuccess = false;
	u8 chTempbuf[4] = { 0 };
	s16 iSensorBuf[3] = {0, };
	int retries = 10;
	struct ssp_data *data = dev_get_drvdata(dev);
	s32 dMsDelay = 20;
	memcpy(&chTempbuf[0], &dMsDelay, 4);

	data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].x = 0;
	data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].y = 0;
	data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].z = 0;

	if (!(atomic64_read(&data->aSensorEnable) & (1 << SENSOR_TYPE_GEOMAGNETIC_FIELD)))
		send_instruction(data, ADD_SENSOR, SENSOR_TYPE_GEOMAGNETIC_FIELD,
			chTempbuf, 4);

	do {
		msleep(60);
		if (check_data_spec(data, SENSOR_TYPE_GEOMAGNETIC_FIELD) == SUCCESS)
			break;
	} while (--retries);

	if (retries > 0)
		bSuccess = true;

	iSensorBuf[0] = data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].x;
	iSensorBuf[1] = data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].y;
	iSensorBuf[2] = data->buf[SENSOR_TYPE_GEOMAGNETIC_FIELD].z;

	if (!(atomic64_read(&data->aSensorEnable) & (1 << SENSOR_TYPE_GEOMAGNETIC_FIELD)))
		send_instruction(data, REMOVE_SENSOR, SENSOR_TYPE_GEOMAGNETIC_FIELD,
			chTempbuf, 4);

	pr_info("[SSP] %s - x = %d, y = %d, z = %d\n", __func__,
		iSensorBuf[0], iSensorBuf[1], iSensorBuf[2]);

	return sprintf(buf, "%s,%d,%d,%d\n", (bSuccess ? "OK" : "NG"),
		iSensorBuf[0], iSensorBuf[1], iSensorBuf[2]);
}

static ssize_t magnetic_get_selftest(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct ssp_data *data = dev_get_drvdata(dev);
	
	if(data->mag_type == AKM_ID)
		return magnetic_get_selftest_akm(dev,attr,buf);
	else
	    return magnetic_get_selftest_yas(dev,attr,buf);
}

static DEVICE_ATTR(name, S_IRUGO, magnetic_name_show, NULL);
static DEVICE_ATTR(vendor, S_IRUGO, magnetic_vendor_show, NULL);
static DEVICE_ATTR(raw_data, S_IRUGO | S_IWUSR | S_IWGRP,
		raw_data_show, raw_data_store);
static DEVICE_ATTR(adc, S_IRUGO, adc_data_read, NULL);
static DEVICE_ATTR(selftest, S_IRUGO, magnetic_get_selftest, NULL);

static DEVICE_ATTR(status, S_IRUGO,  magnetic_get_status, NULL);
static DEVICE_ATTR(dac, S_IRUGO, magnetic_check_cntl, NULL);
static DEVICE_ATTR(ak09911_asa, S_IRUGO, magnetic_get_asa, NULL);
static DEVICE_ATTR(logging_data, S_IRUGO, magnetic_logging_show, NULL);

static DEVICE_ATTR(hw_offset, S_IRUGO, hw_offset_show, NULL);
static DEVICE_ATTR(matrix, S_IRUGO | S_IWUSR | S_IWGRP, matrix_show, matrix_store);

static struct device_attribute *mag_attrs[] = {
	&dev_attr_name,
	&dev_attr_vendor,
	&dev_attr_adc,
	&dev_attr_dac,
	&dev_attr_raw_data,
	&dev_attr_selftest,
	&dev_attr_status,
	&dev_attr_ak09911_asa,
	&dev_attr_logging_data,
	&dev_attr_hw_offset,
	&dev_attr_matrix,
	NULL,
};

int initialize_magnetic_sensor(struct ssp_data *data)
{
	int ret;
	
	if(data->mag_type == AKM_ID)
	{
		/* STATUS AK09916C doesn't need FuseRomdata more*/
#if 0
		ret = get_fuserom_data(data);
		if (ret < 0)
			pr_err("[SSP] %s - get_fuserom_data failed %d\n",
				__func__, ret);
#endif
		data->uFuseRomData[0] = 0;
		data->uFuseRomData[1] = 0;
		data->uFuseRomData[2] = 0;
		
		ret = set_pdc_matrix(data);
		if (ret < 0)
			pr_err("[SSP] %s - set_magnetic_pdc_matrix failed %d\n",
				__func__, ret);
	}
	else
	{
		ret = set_static_matrix(data);
		if (ret < 0)
			pr_err("[SSP]: %s - set_magnetic_static_matrix failed %d\n",
				__func__, ret);
	}
	
	return ret < 0 ? ret : SUCCESS;
}

void initialize_magnetic_factorytest(struct ssp_data *data)
{
	sensors_register(data->devices[SENSOR_TYPE_GEOMAGNETIC_FIELD], data, mag_attrs,
		"magnetic_sensor");
}

void remove_magnetic_factorytest(struct ssp_data *data)
{
	sensors_unregister(data->devices[SENSOR_TYPE_GEOMAGNETIC_FIELD], mag_attrs);
}
