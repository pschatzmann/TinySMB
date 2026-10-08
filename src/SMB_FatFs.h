#pragma once
/**
 * @file SMB_FatFs.h
 * @brief Support for the arduino-fatfs library
 * (https://github.com/pschatzmann/arduino-fatfs): include this instead of
 * SMB.h to export a FatFs volume with FileSystemFatFs.
 *
 * Example: `FileSystemFatFs fat(*SD.getFatFs());`
 */
// SMB.h must come first: fatfs.h redefines FILE_READ and FILE_WRITE
#include "SMB.h"
#include "fatfs.h"
#include "smb-server/SMBFileSystemFatFs.h"
