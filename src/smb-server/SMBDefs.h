#pragma once
#include <stdint.h>

/// Default TCP port of SMB (direct hosting)
#define SMB_DEFAULT_PORT 445

namespace smb {

// SMB2 commands
enum Command : uint16_t {
  SMB2_NEGOTIATE = 0x0000,
  SMB2_SESSION_SETUP = 0x0001,
  SMB2_LOGOFF = 0x0002,
  SMB2_TREE_CONNECT = 0x0003,
  SMB2_TREE_DISCONNECT = 0x0004,
  SMB2_CREATE = 0x0005,
  SMB2_CLOSE = 0x0006,
  SMB2_FLUSH = 0x0007,
  SMB2_READ = 0x0008,
  SMB2_WRITE = 0x0009,
  SMB2_LOCK = 0x000A,
  SMB2_IOCTL = 0x000B,
  SMB2_CANCEL = 0x000C,
  SMB2_ECHO = 0x000D,
  SMB2_QUERY_DIRECTORY = 0x000E,
  SMB2_CHANGE_NOTIFY = 0x000F,
  SMB2_QUERY_INFO = 0x0010,
  SMB2_SET_INFO = 0x0011,
  SMB2_OPLOCK_BREAK = 0x0012,
};

// NT status codes
enum Status : uint32_t {
  STATUS_SUCCESS = 0x00000000,
  STATUS_NO_MORE_FILES = 0x80000006,
  STATUS_BUFFER_OVERFLOW = 0x80000005,
  STATUS_INVALID_PARAMETER = 0xC000000D,
  STATUS_NO_SUCH_FILE = 0xC000000F,
  STATUS_END_OF_FILE = 0xC0000011,
  STATUS_MORE_PROCESSING_REQUIRED = 0xC0000016,
  STATUS_ACCESS_DENIED = 0xC0000022,
  STATUS_BUFFER_TOO_SMALL = 0xC0000023,
  STATUS_OBJECT_NAME_INVALID = 0xC0000033,
  STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034,
  STATUS_OBJECT_NAME_COLLISION = 0xC0000035,
  STATUS_OBJECT_PATH_NOT_FOUND = 0xC000003A,
  STATUS_DELETE_PENDING = 0xC0000056,
  STATUS_LOGON_FAILURE = 0xC000006D,
  STATUS_INSUFFICIENT_RESOURCES = 0xC000009A,
  STATUS_FILE_IS_A_DIRECTORY = 0xC00000BA,
  STATUS_NOT_SUPPORTED = 0xC00000BB,
  STATUS_NETWORK_NAME_DELETED = 0xC00000C9,
  STATUS_BAD_NETWORK_NAME = 0xC00000CC,
  STATUS_INTERNAL_ERROR = 0xC00000E5,
  STATUS_DIRECTORY_NOT_EMPTY = 0xC0000101,
  STATUS_NOT_A_DIRECTORY = 0xC0000103,
  STATUS_CANCELLED = 0xC0000120,
  STATUS_FILE_CLOSED = 0xC0000128,
  STATUS_USER_SESSION_DELETED = 0xC0000203,
  STATUS_NOT_FOUND = 0xC0000225,
  STATUS_INVALID_DEVICE_REQUEST = 0xC0000010,
  STATUS_INVALID_INFO_CLASS = 0xC0000003,
  STATUS_INFO_LENGTH_MISMATCH = 0xC0000004,
  STATUS_DISK_FULL = 0xC000007F,
  STATUS_MEDIA_WRITE_PROTECTED = 0xC00000A2,
  STATUS_FS_DRIVER_REQUIRED = 0xC000019C,
  STATUS_INVALID_HANDLE = 0xC0000008,
};

// SMB2 header flags
const uint32_t SMB2_FLAGS_SERVER_TO_REDIR = 0x00000001;
const uint32_t SMB2_FLAGS_ASYNC_COMMAND = 0x00000002;
const uint32_t SMB2_FLAGS_RELATED_OPERATIONS = 0x00000004;
const uint32_t SMB2_FLAGS_SIGNED = 0x00000008;

// Negotiate
const uint16_t SMB2_NEGOTIATE_SIGNING_ENABLED = 0x0001;
const uint16_t SMB2_NEGOTIATE_SIGNING_REQUIRED = 0x0002;
const uint16_t SMB2_DIALECT_0202 = 0x0202;
const uint16_t SMB2_DIALECT_0210 = 0x0210;
const uint16_t SMB2_DIALECT_WILDCARD = 0x02FF;

// Session flags
const uint16_t SMB2_SESSION_FLAG_IS_GUEST = 0x0001;

// Tree connect
const uint8_t SMB2_SHARE_TYPE_DISK = 0x01;
const uint8_t SMB2_SHARE_TYPE_PIPE = 0x02;

// Create dispositions
enum CreateDisposition : uint32_t {
  FILE_SUPERSEDE = 0,
  FILE_OPEN = 1,
  FILE_CREATE = 2,
  FILE_OPEN_IF = 3,
  FILE_OVERWRITE = 4,
  FILE_OVERWRITE_IF = 5,
};

// Create actions
const uint32_t FILE_SUPERSEDED = 0;
const uint32_t FILE_OPENED = 1;
const uint32_t FILE_CREATED = 2;
const uint32_t FILE_OVERWRITTEN = 3;

// Create options
const uint32_t FILE_DIRECTORY_FILE = 0x00000001;
const uint32_t FILE_NON_DIRECTORY_FILE = 0x00000040;
const uint32_t FILE_DELETE_ON_CLOSE = 0x00001000;

// File attributes
const uint32_t FILE_ATTRIBUTE_READONLY = 0x00000001;
const uint32_t FILE_ATTRIBUTE_HIDDEN = 0x00000002;
const uint32_t FILE_ATTRIBUTE_DIRECTORY = 0x00000010;
const uint32_t FILE_ATTRIBUTE_ARCHIVE = 0x00000020;
const uint32_t FILE_ATTRIBUTE_NORMAL = 0x00000080;

// Info types
const uint8_t SMB2_0_INFO_FILE = 0x01;
const uint8_t SMB2_0_INFO_FILESYSTEM = 0x02;
const uint8_t SMB2_0_INFO_SECURITY = 0x03;
const uint8_t SMB2_0_INFO_QUOTA = 0x04;

// File information classes
enum FileInfoClass : uint8_t {
  FileDirectoryInformation = 1,
  FileFullDirectoryInformation = 2,
  FileBothDirectoryInformation = 3,
  FileBasicInformation = 4,
  FileStandardInformation = 5,
  FileInternalInformation = 6,
  FileEaInformation = 7,
  FileAccessInformation = 8,
  FileRenameInformation = 10,
  FileNamesInformation = 12,
  FileDispositionInformation = 13,
  FilePositionInformation = 14,
  FileModeInformation = 16,
  FileAlignmentInformation = 17,
  FileAllInformation = 18,
  FileAllocationInformation = 19,
  FileEndOfFileInformation = 20,
  FileAlternateNameInformation = 21,
  FileStreamInformation = 22,
  FileCompressionInformation = 28,
  FileNetworkOpenInformation = 34,
  FileAttributeTagInformation = 35,
  FileIdBothDirectoryInformation = 37,
  FileIdFullDirectoryInformation = 38,
  FileNormalizedNameInformation = 48,
  FileDispositionInformationEx = 64,
};

// File system information classes
enum FsInfoClass : uint8_t {
  FileFsVolumeInformation = 1,
  FileFsSizeInformation = 3,
  FileFsDeviceInformation = 4,
  FileFsAttributeInformation = 5,
  FileFsFullSizeInformation = 7,
  FileFsObjectIdInformation = 8,
  FileFsSectorSizeInformation = 11,
};

// Query directory flags
const uint8_t SMB2_RESTART_SCANS = 0x01;
const uint8_t SMB2_RETURN_SINGLE_ENTRY = 0x02;
const uint8_t SMB2_INDEX_SPECIFIED = 0x04;
const uint8_t SMB2_REOPEN = 0x10;

// IOCTL codes
const uint32_t FSCTL_DFS_GET_REFERRALS = 0x00060194;
const uint32_t FSCTL_PIPE_TRANSCEIVE = 0x0011C017;
const uint32_t FSCTL_VALIDATE_NEGOTIATE_INFO = 0x00140204;
const uint32_t SMB2_0_IOCTL_IS_FSCTL = 0x00000001;

// Size of the fixed SMB2 header
const size_t SMB2_HEADER_SIZE = 64;

/// Seconds between 1601-01-01 and 1970-01-01
const uint64_t FILETIME_UNIX_OFFSET = 11644473600ULL;

/// Converts a unix time into a Windows FILETIME
inline uint64_t toFileTime(uint64_t unixTime) {
  if (unixTime == 0) unixTime = 315532800;  // 1980-01-01 (FAT epoch)
  return (unixTime + FILETIME_UNIX_OFFSET) * 10000000ULL;
}

}  // namespace smb
