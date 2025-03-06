/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_rtc

#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/irq.h>
#include "rtc_utils.h"

struct nxp_rtc_config {
	BBNSM_Type *base;
};

/* RTC start time: 1st, Jan, 1970 */
#define RTC_YEAR_REF 1970
/* struct tm start time: 1st, Jan, 1900 */
#define TM_YEAR_REF  1900

#define SECONDS_IN_A_DAY    (86400U)
#define SECONDS_IN_A_HOUR   (3600U)
#define SECONDS_IN_A_MINUTE (60U)
#define DAYS_IN_A_YEAR      (365U)
#define YEAR_RANGE_START    (1970U)
#define YEAR_RANGE_END      (2099U)

/*! @brief Structure is used to hold the date and time */
typedef struct _BBNSM_srtc_datetime
{
    uint16_t year;  /*!< Range from 1970 to 2099.*/
    uint8_t month;  /*!< Range from 1 to 12.*/
    uint8_t day;    /*!< Range from 1 to 31 (depending on month).*/
    uint8_t hour;   /*!< Range from 0 to 23.*/
    uint8_t minute; /*!< Range from 0 to 59.*/
    uint8_t second; /*!< Range from 0 to 59.*/
} BBNSM_srtc_datetime_t;

static void BBNSM_ConvertSecondsToDatetime(uint32_t seconds, BBNSM_srtc_datetime_t *datetime)
{
    assert(datetime != NULL);

    uint32_t x;
    uint32_t secondsRemaining, days;
    uint16_t daysInYear;
    /* Table of days in a month for a non leap year. First entry in the table is not used,
     * valid months start from 1
     */
    uint8_t daysPerMonth[] = {0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

    /* Start with the seconds value that is passed in to be converted to date time format */
    secondsRemaining = seconds;

    /* Calcuate the number of days, we add 1 for the current day which is represented in the
     * hours and seconds field
     */
    days = secondsRemaining / SECONDS_IN_A_DAY + 1U;

    /* Update seconds left*/
    secondsRemaining = secondsRemaining % SECONDS_IN_A_DAY;

    /* Calculate the datetime hour, minute and second fields */
    datetime->hour   = (uint8_t)(secondsRemaining / SECONDS_IN_A_HOUR);
    secondsRemaining = secondsRemaining % SECONDS_IN_A_HOUR;
    datetime->minute = (uint8_t)(secondsRemaining / 60U);
    datetime->second = (uint8_t)(secondsRemaining % SECONDS_IN_A_MINUTE);

    /* Calculate year */
    daysInYear     = DAYS_IN_A_YEAR;
    datetime->year = YEAR_RANGE_START;
    while (days > daysInYear)
    {
        /* Decrease day count by a year and increment year by 1 */
        days -= daysInYear;
        datetime->year++;

        /* Adjust the number of days for a leap year */
        if ((datetime->year & 3U) != 0U)
        {
            daysInYear = DAYS_IN_A_YEAR;
        }
        else
        {
            daysInYear = DAYS_IN_A_YEAR + 1U;
        }
    }

    /* Adjust the days in February for a leap year */
    if (0U == (datetime->year & 3U))
    {
        daysPerMonth[2] = 29U;
    }

    for (x = 1U; x <= 12U; x++)
    {
        if (days <= daysPerMonth[x])
        {
            datetime->month = (uint8_t)x;
            break;
        }
        else
        {
            days -= daysPerMonth[x];
        }
    }

    datetime->day = (uint8_t)days;
}

static uint32_t BBNSM_ConvertDatetimeToSeconds(const BBNSM_srtc_datetime_t *datetime)
{
    assert(datetime != NULL);

    /* Number of days from begin of the non Leap-year*/
    /* Number of days from begin of the non Leap-year*/
    uint16_t monthDays[] = {0U, 0U, 31U, 59U, 90U, 120U, 151U, 181U, 212U, 243U, 273U, 304U, 334U};
    uint32_t days;
    uint32_t seconds;

    /* Compute number of days from 1970 till given year*/
    days = ((uint32_t)datetime->year - 1970U) * DAYS_IN_A_YEAR;
    /* Add leap year days */
    days += (((uint32_t)datetime->year / 4U) - (1970U / 4U));
    /* Add number of days till given month*/
    days += monthDays[datetime->month];
    /* Add days in given month. We subtract the current day as it is
     * represented in the hours, minutes and seconds field*/
    days += ((uint32_t)datetime->day - 1U);
    /* For leap year if month less than or equal to Febraury, decrement day counter*/
    if ((0U == (datetime->year & 3U)) && (datetime->month <= 2U))
    {
        days--;
    }

    seconds = (days * SECONDS_IN_A_DAY) + (datetime->hour * SECONDS_IN_A_HOUR) +
              (datetime->minute * SECONDS_IN_A_MINUTE) + datetime->second;

    return seconds;
}

static int nxp_rtc_set_time(const struct device *dev, const struct rtc_time *timeptr)
{
	const struct nxp_rtc_config *config = dev->config;
	BBNSM_Type *rtc_reg = config->base;
	BBNSM_srtc_datetime_t datetime;
	uint32_t seconds = 0;
    uint32_t real_year = timeptr->tm_year + TM_YEAR_REF;

	if (real_year < RTC_YEAR_REF) {
		/* RTC does not support years before 1970 */
		return -EINVAL;
	}

	if (!timeptr || !rtc_utils_validate_rtc_time(timeptr, 0)) {
		return -EINVAL;
	}

	datetime.year   = real_year;
    /* tm_mon allowed values are 0-11, month range from 1 to 12 */
	datetime.month  = timeptr->tm_mon + 1;
	datetime.day    = timeptr->tm_mday;
	datetime.hour   = timeptr->tm_hour;
	datetime.minute = timeptr->tm_min;
	datetime.second = timeptr->tm_sec;

    seconds =  BBNSM_ConvertDatetimeToSeconds(&datetime);

    /* RTC Disable */
	rtc_reg->BBNSM_CTRL = ((rtc_reg->BBNSM_CTRL)&(~BBNSM_BBNSM_CTRL_RTC_EN_MASK)) | BBNSM_BBNSM_CTRL_RTC_EN(0x1);

	rtc_reg->BBNSM_RTC_MS = seconds >> 17;
    rtc_reg->BBNSM_RTC_LS = seconds << 15;

    /* RTC Enable */
    rtc_reg->BBNSM_CTRL = ((rtc_reg->BBNSM_CTRL)&(~BBNSM_BBNSM_CTRL_RTC_EN_MASK)) | BBNSM_BBNSM_CTRL_RTC_EN(0x2);

	return 0;
}

static int nxp_rtc_get_time(const struct device *dev, struct rtc_time *timeptr)
{
	const struct nxp_rtc_config *config = dev->config;
	BBNSM_Type *rtc_reg = config->base;
	BBNSM_srtc_datetime_t datetime = {0};
	uint32_t seconds = 0;

	__ASSERT(timeptr != 0, "timeptr has not been set");

	seconds = ((rtc_reg->BBNSM_RTC_LS) >> 15) | ((rtc_reg->BBNSM_RTC_MS) << 17);

	BBNSM_ConvertSecondsToDatetime(seconds, &datetime);

	timeptr->tm_sec = datetime.second;
	timeptr->tm_min = datetime.minute;
	timeptr->tm_hour = datetime.hour;
	timeptr->tm_mday = datetime.day;
    /* tm_mon allowed values are 0-11, month range from 1 to 12 */
	timeptr->tm_mon = datetime.month - 1;
	timeptr->tm_year = datetime.year - TM_YEAR_REF;

	/* There is no nano second support for RTC */
	timeptr->tm_nsec = 0;
	/* There is no day of the year support for RTC */
	timeptr->tm_yday = -1;
	timeptr->tm_isdst = -1;
	timeptr->tm_wday = -1;

	return 0;
}

static int nxp_rtc_init(const struct device *dev)
{
	const struct nxp_rtc_config *config = dev->config;
	BBNSM_Type *rtc_reg = config->base;

	/* RTC Enable */
	rtc_reg->BBNSM_CTRL = ((rtc_reg->BBNSM_CTRL)&(~BBNSM_BBNSM_CTRL_RTC_EN_MASK)) | BBNSM_BBNSM_CTRL_RTC_EN(0x2);

	return 0;
}

static DEVICE_API(rtc, rtc_nxp_rtc_driver_api) = {
	.set_time = nxp_rtc_set_time,
	.get_time = nxp_rtc_get_time,
};

#define RTC_NXP_RTC_DEVICE_INIT(n)                                                                \
	static const struct nxp_rtc_config nxp_rtc_config_##n = {                                \
		.base = (BBNSM_Type *)DT_INST_REG_ADDR(n),                                           \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, nxp_rtc_init, NULL, NULL, &nxp_rtc_config_##n,    \
			      PRE_KERNEL_1, CONFIG_RTC_INIT_PRIORITY, &rtc_nxp_rtc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RTC_NXP_RTC_DEVICE_INIT)
