//                              -*- Mode: C++ -*- 
// 
// Description: 
// 
// Author: 
// Created: 2016/6/23 ������ 17:31:37
// Last-Updated: 2016/6/28 ���ڶ� 18:11:08
//           By: 10034491
// 
//     Update #: 4
// 

// Change Log:
// 
#ifndef _GETTIMEOFDAY_MOCK_H_
#define _GETTIMEOFDAY_MOCK_H_

#include <time.h>
#include <windows.h>

struct timezone
{
    int tz_minuteswest; /* ��Greewichʱ����˶��ٷ���*/
    int tz_dsttime; /* �չ��Լʱ���״̬ */
};

static int gettimeofday(struct timeval *tp, struct timezone *tzp)
{
    time_t clock;
    struct tm tm;
    SYSTEMTIME wtm;
 
    GetLocalTime(&wtm);
    tm.tm_year     = wtm.wYear - 1900;
    tm.tm_mon     = wtm.wMonth - 1;
    tm.tm_mday     = wtm.wDay;
    tm.tm_hour     = wtm.wHour;
    tm.tm_min     = wtm.wMinute;
    tm.tm_sec     = wtm.wSecond;
    tm. tm_isdst    = -1;
    clock = mktime(&tm);
    tp->tv_sec = clock;
    tp->tv_usec = wtm.wMilliseconds * 1000;

    long seconds;
    _get_timezone(&seconds);
    // tzp->tz_minuteswest = seconds/60;

    int hours;
    _get_daylight(&hours);
    // tzp->tz_dsttime; // FIXME ����ط���ʱû�� ����ʱ
    
    return (0);
}


#endif//_GETTIMEOFDAY_MOCK_H_
