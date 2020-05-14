/* @@@LICENSE
 * *
 * * Copyright (c) 2020 LG Electronics, Inc.
 * *
 * * Licensed under the Apache License, Version 2.0 (the "License");
 * * you may not use this file except in compliance with the License.
 * * You may obtain a copy of the License at
 * *
 * * http://www.apache.org/licenses/LICENSE-2.0
 * *
 * * Unless required by applicable law or agreed to in writing, software
 * * distributed under the License is distributed on an "AS IS" BASIS,
 * * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * * See the License for the specific language governing permissions and
 * * limitations under the License.
 * *
 * * LICENSE@@@ */

/*
 * *******************************************************************/

#include "parser_nmea.h"

#include <cstring>
#include <sys/time.h>
#include <thread>
#include <time.h>

#include <nyx/module/nyx_log.h>

#include "gps_storage.h"
#include "parser_inotify.h"
#include "parser_interface.h"
#include "parser_thread_pool.h"

const std::string nmea_file_path ="/media/internal/location";
const std::string nmea_file_name ="gps.nmea";
const std::string nmea_complete_path = nmea_file_path + "/" + nmea_file_name;

int64_t getCurrentTime() {
    struct timeval tval;
    gettimeofday(&tval, (struct timezone *) NULL);
    return (tval.tv_sec * 1000LL + tval.tv_usec/1000);
}

void ParserNmea::sendLocationUpdates() {
    GpsLocation location;
    memset(&location, 0, sizeof(GpsLocation));

    location.latitude = mGpsData.latitude;
    location.longitude = mGpsData.longitude;
    location.altitude = mGpsData.altitude;
    location.speed = mGpsData.speed;
    location.accuracy = mGpsData.horizAccuracy;
    location.timestamp = getCurrentTime();

    parser_loc_cb(&location, nullptr);
}

void ParserNmea::sendNmeaUpdates(char * rawNmea) {
    if (rawNmea)
        parser_nmea_cb(getCurrentTime(), rawNmea, (int)strlen(rawNmea));
}

bool ParserNmea::SetGpsGGA_Data(CNMEAParserData::GGA_DATA_T& ggaData, char *nmea_data) {

    nyx_debug("GPGGA Parsed!\n");
    nyx_debug("   Time:                %02d:%02d:%02d\n", ggaData.m_nHour, ggaData.m_nMinute, ggaData.m_nSecond);
    nyx_debug("   Latitude:            %f\n", ggaData.m_dLatitude);
    nyx_debug("   Longitude:           %f\n", ggaData.m_dLongitude);
    nyx_debug("   Altitude:            %.01fM\n", ggaData.m_dAltitudeMSL);
    nyx_debug("   GPS Quality:         %d\n", ggaData.m_nGPSQuality);
    nyx_debug("   Satellites in view:  %d\n", ggaData.m_nSatsInView);
    nyx_debug("   HDOP:                %.02f\n", ggaData.m_dHDOP);
    nyx_debug("   Differential ID:     %d\n", ggaData.m_nDifferentialID);
    nyx_debug("   Differential age:    %f\n", ggaData.m_dDifferentialAge);
    nyx_debug("   Geoidal Separation:  %f\n", ggaData.m_dGeoidalSep);
    nyx_debug("   Vertical Speed:      %.02f\n", ggaData.m_dVertSpeed);

    mGpsData.latitude = ggaData.m_dLatitude;
    mGpsData.longitude = ggaData.m_dLongitude;
    mGpsData.altitude = ggaData.m_dAltitudeMSL;
    mGpsData.horizAccuracy = ggaData.m_dHDOP;

    sendLocationUpdates();
    sendNmeaUpdates(nmea_data);
    return CNMEAParserData::ERROR_OK;
}

bool ParserNmea::SetGpsGSV_Data(CNMEAParserData::GSV_DATA_T& gsvData, char *nmea_data) {
    GpsSvStatus sv_status;
    memset(&sv_status, 0, sizeof(GpsSvStatus));

    sv_status.num_svs = gsvData.nSatsInView;
    for(auto i = 0; i < sv_status.num_svs; i++)
    {
        sv_status.sv_list[i].prn = gsvData.SatInfo[i].nPRN;
        sv_status.sv_list[i].snr = gsvData.SatInfo[i].nSNR;
        sv_status.sv_list[i].elevation = gsvData.SatInfo[i].dElevation;
        sv_status.sv_list[i].azimuth = gsvData.SatInfo[i].dAzimuth;

        nyx_debug("    GPS No of Satellites: %d\n", sv_status.num_svs);
        nyx_debug("    GPS PRN: %d\n", gsvData.SatInfo[i].nPRN);
        nyx_debug("    GPS SNR: %d\n", gsvData.SatInfo[i].nSNR);
        nyx_debug("    GPS Elevation: %f\n", gsvData.SatInfo[i].dElevation);
        nyx_debug("    GPS azimuth: %f\n", gsvData.SatInfo[i].dAzimuth);
    }

    parser_sv_cb(&sv_status, nullptr);

    sendNmeaUpdates(nmea_data);
    return CNMEAParserData::ERROR_OK;
}

bool ParserNmea::SetGpsGSA_Data(CNMEAParserData::GSA_DATA_T& gsaData, char *nmea_data) {
    nyx_debug("    nAutoMode: %d\n", gsaData.nAutoMode);
    nyx_debug("    nMode: %d\n", gsaData.nMode);
    nyx_debug("    GPS dPDOP: %f\n", gsaData.dPDOP);
    nyx_debug("    GPS dHDOP: %f\n", gsaData.dHDOP);
    nyx_debug("    GPS dVDOP: %f\n", gsaData.dVDOP);
    nyx_debug("    GPS uGGACount: %u\n", gsaData.uGGACount);

    sendNmeaUpdates(nmea_data);

    return CNMEAParserData::ERROR_OK;
}

bool ParserNmea::SetGpsRMC_Data(CNMEAParserData::RMC_DATA_T& rmcData, char *nmea_data) {
    nyx_debug("GPRMC Parsed!\n");
    nyx_debug("   m_timeGGA:            %ld\n", rmcData.m_timeGGA);
    nyx_debug("   Time:                %02d:%02d:%02d\n", rmcData.m_nHour, rmcData.m_nMinute, rmcData.m_nSecond);
    nyx_debug("   Seconds:            %f\n", rmcData.m_dSecond);
    nyx_debug("   Latitude:            %f\n", rmcData.m_dLatitude);
    nyx_debug("   Longitude:           %f\n", rmcData.m_dLongitude);
    nyx_debug("   Altitude:            %.01fM\n", rmcData.m_dAltitudeMSL);
    nyx_debug("   Speed:           %f\n", rmcData.m_dSpeedKnots);
    nyx_debug("   TrackAngle:           %f\n", rmcData.m_dTrackAngle);

    nyx_debug("   m_nMonth:         %d\n", rmcData.m_nMonth);
    nyx_debug("   m_nDay:  %d\n", rmcData.m_nDay);
    nyx_debug("   m_nYear :     %d\n", rmcData.m_nYear);
    nyx_debug("   m_dMagneticVariation:    %f\n", rmcData.m_dMagneticVariation);

/*    struct tm timeStamp = {rmcData.m_nSecond, rmcData.m_nMinute, rmcData.m_nHour,
                                                rmcData.m_nDay, rmcData.m_nMonth, (rmcData.m_nYear-1900)};
    nyx_debug("   timeStamp:    %ld\n", ((mktime(&timeStamp)+(long)(rmcData.m_dSecond*1000))*1000));
*/
    mGpsData.latitude = rmcData.m_dLatitude;
    mGpsData.longitude = rmcData.m_dLongitude;
    mGpsData.altitude = rmcData.m_dAltitudeMSL;
    mGpsData.speed = rmcData.m_dSpeedKnots*0.514;
    mGpsData.direction = rmcData.m_dTrackAngle;

    sendLocationUpdates();
    sendNmeaUpdates(nmea_data);

    return CNMEAParserData::ERROR_OK;
}

void SetGpsStatus(int status)
{
    GpsStatus gps_status;
    memset(&gps_status, 0, sizeof(GpsStatus));
    gps_status.status = status;
    parser_status_cb(&gps_status, nullptr);
}

CNMEAParserData::ERROR_E ParserNmea::ProcessRxCommand(char *pCmd, char *pData, char *checksum) {
    // Call base class to process the command
    CNMEAParser::ProcessRxCommand(pCmd, pData);

    nyx_debug("Cmd: %s\nData: %s, checksum:%.2s\n", pCmd, pData, checksum);
    char *nmea_data = NULL;
    int len = strlen(pCmd) + strlen(pData) + 7;
    nmea_data = (char *)malloc(len);
    snprintf(nmea_data, len, "$%.5s,%s*%.2s", pCmd, pData, checksum);

    // Check if this is the GPGGA command. If it is, then set gps location
    if (strstr(pCmd, "GPGGA") != NULL) {
	CNMEAParserData::GGA_DATA_T* ggaData =  (CNMEAParserData::GGA_DATA_T*)malloc(sizeof(CNMEAParserData::GGA_DATA_T));
        if (GetGPGGA(*ggaData) == CNMEAParserData::ERROR_OK) {
            mParserThreadPoolObj->enqueue([=](){
                SetGpsGGA_Data(*ggaData, nmea_data);
                free(nmea_data);
            });
        }
    }
    else if (strstr(pCmd, "GPGSV") != NULL) { //GPS GSV Data
        CNMEAParserData::GSV_DATA_T* gsvData = (CNMEAParserData::GSV_DATA_T*)malloc(sizeof(CNMEAParserData::GSV_DATA_T));
        if (GetGPGSV(*gsvData) == CNMEAParserData::ERROR_OK) {
            mParserThreadPoolObj->enqueue([=](){
                SetGpsGSV_Data(*gsvData, nmea_data);
                free(nmea_data);
            });
         }
    }
    else if (strstr(pCmd, "GPGSA") != NULL) {
        CNMEAParserData::GSA_DATA_T* gsaData = (CNMEAParserData::GSA_DATA_T*)malloc(sizeof(CNMEAParserData::GSA_DATA_T));
        if (GetGPGSA(*gsaData) == CNMEAParserData::ERROR_OK) {
            mParserThreadPoolObj->enqueue([=](){
                SetGpsGSA_Data(*gsaData, nmea_data);
                free(nmea_data);
            });
         }
    }
    else if (strstr(pCmd, "GPRMC") != NULL) {
        CNMEAParserData::RMC_DATA_T* rmcData = (CNMEAParserData::RMC_DATA_T*)malloc(sizeof(CNMEAParserData::RMC_DATA_T));
        if(GetGPRMC(*rmcData) == CNMEAParserData::ERROR_OK) {
            mParserThreadPoolObj->enqueue([=](){
                SetGpsRMC_Data(*rmcData, nmea_data);
                free(nmea_data);
            });
         }
    }
    else {
        if (nmea_data)
            free(nmea_data);
    }

    return CNMEAParserData::ERROR_OK;
}

void ParserNmea::OnError(CNMEAParserData::ERROR_E nError, char *pCmd) {
}

ParserNmea::ParserNmea()
    :    mNmeaFp(nullptr)
    ,    mSeekOffset(0)
    ,    mStopParser(false)
    ,    mParserThreadPoolObj(nullptr) {
    memset(&mGpsData, 0, sizeof(mGpsData));
    parser_inotify_init();
}

ParserNmea::~ParserNmea() {
    parser_inotify_cleanup();

    if (mParserThreadPoolObj) {
        delete mParserThreadPoolObj;
        mParserThreadPoolObj = nullptr;
    }

    if (mNmeaFp) {
        fclose(mNmeaFp);
    }
}

ParserNmea* ParserNmea::getInstance() {
    static ParserNmea parserNmeaObj;
    return &parserNmeaObj;
}

void ParserNmea::init() {
    mGpsData.altitude = -1;
    mGpsData.speed = -1;
    mGpsData.direction = -1;
    mGpsData.horizAccuracy = -1;
}

void ParserNmea::deinit() {
    ResetData();
    memset(&mGpsData, 0, sizeof(mGpsData));
}

static void parser_inotify_handler(struct inotify_event *event,
                                        const char *ident)
{
    if (!ident)
        return;

    if ((strlen(ident) != nmea_file_name.size()) || strncmp(ident, nmea_file_name.c_str(), nmea_file_name.size()) != 0)
        return;

    if (event->mask & (IN_MODIFY | IN_MOVED_TO)) {
        parser_inotify_unregister(nmea_file_path.c_str(), parser_inotify_handler);
        startParsing();
    }
}

bool ParserNmea::startParsing() {
    mNmeaFp = fopen(nmea_complete_path.c_str(), "r");
    if (mNmeaFp == nullptr) {
        nyx_error("MSGID_NMEA_PARSER", 0, "Fun: %s, Line: %d Could not open file: %s \n", __FUNCTION__, __LINE__, nmea_file_path);
        return false;
    }

    init();
    SetGpsStatus(NYX_GPS_STATUS_SESSION_BEGIN);

    GKeyFile *keyfile = gps_config_load_file();
    if (!keyfile) {
        nyx_error("MSGID_NMEA_PARSER", 0, "mock config file not available \n");
        return false;
    }

    int latency, interval;
    latency = g_key_file_get_integer(keyfile, GPS_MOCK_INFO, "LATENCY", NULL);
    if (!latency) {
        nyx_debug("config file latency not available so default latency:%d\n", DEFAULT_LATENCY);
        latency = DEFAULT_LATENCY;
    }

    g_key_file_free(keyfile);

    interval = latency/2;

    if (!mParserThreadPoolObj) {
        mParserThreadPoolObj = new ParserThreadPool(1, interval);
    }

    if (mSeekOffset) {
        fseek(mNmeaFp, mSeekOffset, SEEK_SET);
    }

    char pBuff[1024];
    while (mNmeaFp && feof(mNmeaFp) == 0) {

        if (mStopParser)
        {
            mStopParser = false;
            fclose(mNmeaFp);
            mNmeaFp = nullptr;
            return true;
        }

        memset(&pBuff, 0, sizeof(pBuff));
        size_t nBytesRead = fread(pBuff, 1, 512, mNmeaFp);

        CNMEAParserData::ERROR_E nErr;
        if ((nErr = ProcessNMEABuffer(pBuff, nBytesRead)) != CNMEAParserData::ERROR_OK) {
            nyx_error("MSGID_NMEA_PARSER", 0, "Fun: %s, Line: %d error: %d \n", __FUNCTION__, __LINE__, nErr);
            return false;
        }
        mSeekOffset += nBytesRead;
    }

    if (mNmeaFp) {
        fclose(mNmeaFp);
        mNmeaFp = nullptr;
    }

    parser_inotify_register(nmea_file_path.c_str(), parser_inotify_handler);

    return false;
}

bool ParserNmea::stopParsing() {
    mSeekOffset = 0;
    parser_inotify_unregister(nmea_file_path.c_str(), parser_inotify_handler);

    if (mParserThreadPoolObj) {
        delete mParserThreadPoolObj;
        mParserThreadPoolObj = nullptr;
    }

    if (mNmeaFp) {
        mStopParser = true;
        fclose(mNmeaFp);
        mNmeaFp = nullptr;
    }

    SetGpsStatus(NYX_GPS_STATUS_SESSION_END);

    deinit();

    return true;
}
