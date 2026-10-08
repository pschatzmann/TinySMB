#pragma once
/**
 * @file SMB.h
 * @brief Header-only SMB2 (Windows file sharing) server and client for
 * Arduino.
 *
 * For SD card support include SMB_SD.h (SD library), SMB_SDMMC.h (SD_MMC
 * library) or SMB_FatFs.h (arduino-fatfs library) instead.
 */
#include "smb-server/SMBFileSystem.h"
#include "smb-server/SMBFileSystemSD.h"
#include "smb-server/SMBLogger.h"
#include "smb-server/SMBServer.h"
#include "smb-client/SMBClient.h"
#if defined(ESP32)
#include "smb-server/SMBFileSystemFS.h"
#include "smb-client/SMBClientFS.h"
#endif
#if defined(ESP32) || defined(IS_DESKTOP)
#include "smb-server/SMBFileSystemPosix.h"
#endif

#ifndef SMB_NO_USING_NAMESPACE
using namespace smb;
#endif
