#include<ntstatus.h>

#include<cstdio>
#include<cstring>
#include<cwchar>
#include<cerrno>
#include<cassert>
#include<map>
#include<vector>
#include"Logger.h"
#include"../FS/CFilesystem.h"

#include"dokanoper.h"

extern "C" {
    #include <dokan/dokan.h>
}

static CFilesystem *fs;
static uint64_t handleid = 1;
bool mounted = false;

static std::map<ULONG64, std::string> handle2path;

// -----------------------------------------------

#include <boost/locale/encoding_utf.hpp>
using boost::locale::conv::utf_to_utf;

std::wstring utf8_to_wstring(const std::string& str)
{
    return utf_to_utf<wchar_t>(str.c_str(), str.c_str() + str.size());
}

std::string wstring_to_utf8(const std::wstring& str)
{
    return utf_to_utf<char>(str.c_str(), str.c_str() + str.size());
}

std::string GetFilePath(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
    if (DokanFileInfo->Context == 0)
    {
        return wstring_to_utf8(FileName);
    } else
    {
        return handle2path[DokanFileInfo->Context];
    }
}

// -----------------------------------------------

NTSTATUS errno_to_nstatus(int err)
{
    if (err < 0) err = -err;
    switch(err)
    {
    case ENOENT:
        return STATUS_OBJECT_NAME_NOT_FOUND;
    case EEXIST:
        return STATUS_OBJECT_NAME_COLLISION;
    case ENOMEM:
        return STATUS_NO_MEMORY;
    case ENOTEMPTY:
        return STATUS_DIRECTORY_NOT_EMPTY;
    case ENOTDIR:
        return STATUS_OBJECT_NAME_NOT_FOUND;
    default: break;
    }
    return ERROR_INVALID_FUNCTION;
}
// -----------------------------------------------

static NTSTATUS DOKAN_CALLBACK
Dokan_CreateFile(
    LPCWSTR FileName,
    PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
    ACCESS_MASK DesiredAccess,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = wstring_to_utf8(FileName);
    std::ostringstream os;

    os << "Dokan: CreateFile '" << path << "' (new handle id " << handleid << ") ";

    if (path.empty()) return STATUS_OBJECT_NAME_NOT_FOUND;

    if (CreateOptions&FILE_DIRECTORY_FILE) os << " (isdir) ";

    // Specifies the action to perform if the file does or does not exist.
    switch(CreateDisposition)
    {
    case CREATE_NEW:
        os << "(create new)";
        break;

    case OPEN_ALWAYS:
        os << "(open always)";
        break;

    case CREATE_ALWAYS:
        os << "(create always)";
        break;

    case OPEN_EXISTING:
        os << "(open existing)";
        break;

    case TRUNCATE_EXISTING:
        os << "(Truncate existing)";
        break;

    default:
        os << "(unknown)";
        break;
    }

    if ((DesiredAccess&GENERIC_READ)) os << "(generic read)";
    if ((DesiredAccess&GENERIC_WRITE)) os << "(generic write)";
    if ((DesiredAccess&GENERIC_EXECUTE)) os << "(generic execute)";

    if ((DesiredAccess&DELETE)) os << "(delete)";
    if ((DesiredAccess&FILE_READ_DATA)) os << "(read data)";
    if ((DesiredAccess&FILE_READ_ATTRIBUTES)) os << "(read attributes)";
    if ((DesiredAccess&FILE_READ_EA)) os << "(read ea)";
    //if ((DesiredAccess&FILE_READ_CONTROL)) os << "(read control)";
    if ((DesiredAccess&FILE_WRITE_DATA)) os << "(write data)";
    if ((DesiredAccess&FILE_WRITE_ATTRIBUTES)) os << "(write attributes)";
    if ((DesiredAccess&FILE_WRITE_EA)) os << "(write ea)";
    if ((DesiredAccess&FILE_APPEND_DATA)) os << "(append data)";
    //if ((DesiredAccess&FILE_WRITE_DAC)) os << "(write dac)";
    //if ((DesiredAccess&FILE_WRITE_OWNER)) os << "(write owner)";
    if ((DesiredAccess&SYNCHRONIZE)) os << "(synchronize)";
    if ((DesiredAccess&FILE_EXECUTE)) os << "(file execute)";
    if ((DesiredAccess&STANDARD_RIGHTS_READ)) os << "(standard rights read)";
    if ((DesiredAccess&STANDARD_RIGHTS_WRITE)) os << "(standard rights write)";
    if ((DesiredAccess&STANDARD_RIGHTS_EXECUTE)) os << "(standard rights execute)";
    if ((DesiredAccess&FILE_LIST_DIRECTORY)) os << "(file list directory)";
    if ((DesiredAccess&FILE_TRAVERSE)) os << "(file traverse)";
    LOG(LogLevel::INFO) << os.str();
    
    if ((CreateOptions & FILE_DIRECTORY_FILE) == FILE_DIRECTORY_FILE)
    {
        if (CreateDisposition == FILE_CREATE || CreateDisposition == FILE_OPEN_IF)
        {
            //LOG(LogLevel::INFO) << "command: create directory";

            std::vector<std::string> splitpath = CPath(path).GetPath();

            if (splitpath.size() == 0)
            {
                SetLastError(ERROR_ALREADY_EXISTS);
                DokanFileInfo->IsDirectory = TRUE;
                DokanFileInfo->Context = handleid++;
                handle2path[DokanFileInfo->Context] = path;
                return STATUS_SUCCESS;
            }
            //assert(splitpath.size() >= 1);

            std::string filename = splitpath.back();
            splitpath.pop_back();
            try
            {
                CDirectoryPtr dir = fs->OpenDir(CPath(splitpath));
                dir->MakeDirectory(filename);
                DokanFileInfo->Context = handleid++;
                DokanFileInfo->IsDirectory = TRUE;
                handle2path[DokanFileInfo->Context] = path;
            } catch(const int &err) // or file already exist?
            {
                return errno_to_nstatus(err);
            }
            return STATUS_SUCCESS;
        } else
        if (CreateDisposition == FILE_OPEN)
        {
            DokanFileInfo->IsDirectory = TRUE;

            //LOG(LogLevel::INFO) << "command: open directory";
            try
            {
                CDirectoryPtr node = fs->OpenDir(CPath(path)); // just to check if everything works and preload
                DokanFileInfo->Context = handleid++;
                handle2path[DokanFileInfo->Context] = path;
            } catch(const int &err)
            {
                return errno_to_nstatus(err);
            }
            return STATUS_SUCCESS;
        }
    }

    if (CreateDisposition == FILE_CREATE || CreateDisposition == FILE_OPEN_IF)
    {
        //LOG(LogLevel::INFO) << "Dokan: command: create file";

        std::vector<std::string> splitpath = CPath(path).GetPath();
        assert(splitpath.size() >= 1);
        std::string filename = splitpath.back();
        splitpath.pop_back();
        try
        {
            CDirectoryPtr dir = fs->OpenDir(CPath(splitpath));
            dir->MakeFile(filename);
            DokanFileInfo->Context = handleid++;
            handle2path[DokanFileInfo->Context] = path;
        } catch(const int &err) // or file already exist?
        {
            return errno_to_nstatus(err);
        }

        //SetLastError(ERROR_ALREADY_EXISTS);
        return STATUS_SUCCESS;
        //return errno_to_ntstatus_error(impl->create_directory(FileName, DokanFileInfo));
    } else
    if (CreateDisposition == FILE_OPEN)
    {
        //LOG(LogLevel::INFO) << "Dokan: command: open file";
        try
        {
            CInodePtr node = fs->OpenFile(CPath(path));
            DokanFileInfo->Context = handleid++;
            handle2path[DokanFileInfo->Context] = path;
        } catch(const int &err)
        {
            return errno_to_nstatus(err);
        }
        return STATUS_SUCCESS;
    }

    return STATUS_SUCCESS;
}

// -----------------------------------------------

static NTSTATUS DOKAN_CALLBACK Dokan_GetFileInformation(LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation, PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    LOG(LogLevel::INFO) << "Dokan: GetFileInformation '" << path << "' of handle " << DokanFileInfo->Context;

    HANDLE handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE)
    {
        LOG(LogLevel::INFO) << "Dokan: Invalid handle?";
    }

    //memset(HandleFileInformation, 0, sizeof(struct BY_HANDLE_FILE_INFORMATION));
    try
    {
        CInodePtr node = fs->OpenNode(CPath(path));
        //LOG(LogLevel::INFO) << "open succesful";

        int64_t size = node->GetSize();
        HandleFileInformation->nFileSizeLow = size&0xFFFFFFFF;
        HandleFileInformation->nFileSizeHigh = size >> 32;
        HandleFileInformation->nNumberOfLinks = 1;
        //HandleFileInformation->ftCreationTime = 0;
        //HandleFileInformation->ftLastAccessTime = 0;
        //HandleFileInformation->ftLastWriteTime = 0;
        HandleFileInformation->nFileIndexHigh = 0;
        HandleFileInformation->nFileIndexLow = node->GetId();
        HandleFileInformation->dwVolumeSerialNumber = 0x53281900;

        // FILE_ATTRIBUTE_OFFLINE
        if (node->GetType() == INODETYPE::dir)
        {
            HandleFileInformation->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
            DokanFileInfo->IsDirectory = TRUE;
        } else
        {
            HandleFileInformation->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        }

    } catch(const int &err)
    {
        return errno_to_nstatus(err);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK Dokan_SetFileAttributes(
LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    LOG(LogLevel::INFO) << "Dokan: SetFileAttributes '" << path << "' FileAttributes: " << FileAttributes;
    return STATUS_SUCCESS;
}
/*
static NTSTATUS DOKAN_CALLBACK Dokan_GetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
    PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    printf("GetFileSecurity '%s' requested information: 0x%08x\n", path.c_str(), *SecurityInformation);


    if ((*SecurityInformation) & OWNER_SECURITY_INFORMATION)
    {
	printf("request owner security information\n");
    }
    if ((*SecurityInformation) & GROUP_SECURITY_INFORMATION)
    {
	printf("request group security information\n");
    }
    if ((*SecurityInformation) & DACL_SECURITY_INFORMATION)
    {
	printf("request dacl security information\n");
    }
    if ((*SecurityInformation) > 7)
    {
        return ERROR_INVALID_FUNCTION;
    }
    return ERROR_INVALID_FUNCTION;
    //return STATUS_SUCCESS;
}
*/
/*
NTSTATUS DOKAN_CALLBACK
Dokan_FindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
                PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = wstring_to_utf8(FileName);
    LOG(LogLevel::INFO) << "FindStreams '" << path << "'";
    return STATUS_SUCCESS;
}
*/

// -----------------------------------------------

static NTSTATUS DOKAN_CALLBACK Dokan_SetEndOfFile(
    LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    LOG(LogLevel::INFO) << "Dokan: SetEndOfFile '" << path << "' size=" <<  ByteOffset;

    try
    {
        CInodePtr node = fs->OpenFile(CPath(path));
        node->Truncate(ByteOffset, false);  // the content is undefined according to spec
        //node->Truncate(ByteOffset);
    } catch(const int &err)
    {
        return errno_to_nstatus(err);
    }

    return STATUS_SUCCESS;
}

// -----------------------------------------------

static NTSTATUS DOKAN_CALLBACK Dokan_ReadFile(LPCWSTR FileName, LPVOID Buffer,
DWORD BufferLength,
LPDWORD ReadLength,
LONGLONG Offset,
PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    LOG(LogLevel::INFO) << "Dokan: ReadFile '" << path << "' size=" << BufferLength;
    try
    {
        CInodePtr node = fs->OpenFile(CPath(path));
        *ReadLength = node->Read((int8_t*)Buffer, Offset, BufferLength);
    } catch(const int &err)
    {
        return errno_to_nstatus(err);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK Dokan_WriteFile(LPCWSTR FileName, LPCVOID Buffer,
DWORD NumberOfBytesToWrite,
LPDWORD NumberOfBytesWritten,
LONGLONG Offset,
PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    LOG(LogLevel::INFO) << "Dokan: WriteFile '" << path << "' size=" << NumberOfBytesToWrite;
    try
    {
        CInodePtr node = fs->OpenFile(CPath(path));
        node->Write((int8_t*)Buffer, Offset, NumberOfBytesToWrite);
        *NumberOfBytesWritten = NumberOfBytesToWrite;
    } catch(const int &err)
    {
        return errno_to_nstatus(err);
    }
    return STATUS_SUCCESS;
}


static NTSTATUS DOKAN_CALLBACK
Dokan_FindFiles(LPCWSTR FileName, PFillFindData FillFindData, PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    LOG(LogLevel::INFO) << "Dokan: FindFiles '" << path << "'";

    try
    {
        CDirectoryPtr dir = fs->OpenDir(CPath(path));
        CDirectoryIteratorPtr iterator = dir->GetIterator();
        while(iterator->HasNext())
        {
            WIN32_FIND_DATAW findData = {0};

            CDirectoryEntry de = iterator->Next();

            CInodePtr node = fs->OpenNode(de.id);
            int64_t size = node->GetSize();
            findData.nFileSizeHigh = size >> 32;
            findData.nFileSizeLow = size & 0xFFFFFFFF;
            if (node->GetType() == INODETYPE::dir)
            {
            findData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
            } else
            {
            findData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            }
            std::wstring name = utf8_to_wstring(de.name);
            wcsncpy(findData.cFileName, name.data(), 96+32);
            FillFindData(&findData, DokanFileInfo);

        }

    } catch(const int &err)
    {
        return errno_to_nstatus(err);
    }

    return STATUS_SUCCESS;
}

// -----------------------------------------------

static NTSTATUS DOKAN_CALLBACK
Dokan_MoveFile(LPCWSTR FileName, // existing file name
LPCWSTR NewFileName, BOOL ReplaceIfExisting, PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string oldpath = GetFilePath(FileName, DokanFileInfo);
    std::string newpath = wstring_to_utf8(NewFileName);
    LOG(LogLevel::INFO) << "Dokan: MoveFile '" << oldpath << "' to '" << newpath << "'";

    std::vector<std::string> splitpath = CPath(newpath).GetPath();
    assert(!splitpath.empty());

    try
    {
        std::string filename = splitpath.back();
        splitpath.pop_back();
        CDirectoryPtr dir = fs->OpenDir(CPath(splitpath));
        fs->Rename(CPath(oldpath), dir, filename);
    } catch(const int &err)
    {
        return errno_to_nstatus(err);
    }

    /*
    WCHAR filePath[MAX_PATH];
    WCHAR newFilePath[MAX_PATH];
    BOOL status;

    GetFilePath(filePath, MAX_PATH, FileName);
    GetFilePath(newFilePath, MAX_PATH, NewFileName);

    DbgPrint(L"MoveFile %s -> %s\n\n", filePath, newFilePath);

    if (DokanFileInfo->Context) {
        // should close? or rename at closing?
        CloseHandle((HANDLE)DokanFileInfo->Context);
        DokanFileInfo->Context = 0;
    }

    if (ReplaceIfExisting)
    status = MoveFileEx(filePath, newFilePath, MOVEFILE_REPLACE_EXISTING);
    else
    status = MoveFile(filePath, newFilePath);

    if (status == FALSE) {
        DWORD error = GetLastError();
        DbgPrint(L"\tMoveFile failed status = %d, code = %d\n", status, error);
        return ToNtStatus(error);
    } else {
        return STATUS_SUCCESS;
    }
    */
    return STATUS_SUCCESS;
}

// -----------------------------------------------

static void DOKAN_CALLBACK Dokan_CloseFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    LOG(LogLevel::INFO) << "Dokan: CloseFile '" << path << "' of handle " << DokanFileInfo->Context;
    if (DokanFileInfo)
    if (DokanFileInfo->Context)
    {
        DokanFileInfo->Context = 0;
    }
}

static void DOKAN_CALLBACK Dokan_Cleanup(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
    std::string path = GetFilePath(FileName, DokanFileInfo);
    LOG(LogLevel::INFO) << "Dokan: Cleanup '" << path << "' of handle " << DokanFileInfo->Context;

    if (!DokanFileInfo->Context) return;

    if (!DokanFileInfo->DeleteOnClose) return;
    LOG(LogLevel::INFO) << "Dokan: remove file";
    try
    {
        fs->Unlink(CPath(path));
    } catch(const int &err)
    {
        LOG(LogLevel::INFO) << "Dokan: Cannot remove file";
        return;
    }
}

// -----------------------------------------------

static NTSTATUS DOKAN_CALLBACK
Dokan_DeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) 
{
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
Dokan_DeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) 
{
    return STATUS_SUCCESS;
}

// -----------------------------------------------

static NTSTATUS DOKAN_CALLBACK Dokan_GetVolumeInformation(
LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
PDOKAN_FILE_INFO DokanFileInfo)
{
    UNREFERENCED_PARAMETER(DokanFileInfo);

    LOG(LogLevel::INFO) << "Dokan: GetVolumeInformation";
    wcsncpy(VolumeNameBuffer, L"CoverFS", VolumeNameSize);

    *VolumeSerialNumber = 0x53281900;
    *MaximumComponentLength = 255;

    *FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES |
    FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK  |
    FILE_PERSISTENT_ACLS;

    //*FileSystemFlags = 3;
    wcsncpy(FileSystemNameBuffer, L"Dokan", FileSystemNameSize);

    return STATUS_SUCCESS;
}
/*
static NTSTATUS DOKAN_CALLBACK Dokan_LockFile(LPCWSTR FileName,
                                            LONGLONG ByteOffset,
                                            LONGLONG Length,
                                            PDOKAN_FILE_INFO DokanFileInfo)
{
    printf("LockFile '%s'\n", wstring_to_utf8(FileName).c_str());
    return STATUS_SUCCESS;
}
*/
// -----------------------------------------------

static NTSTATUS DOKAN_CALLBACK Dokan_Mounted(PDOKAN_FILE_INFO DokanFileInfo)
{
    UNREFERENCED_PARAMETER(DokanFileInfo);

    LOG(LogLevel::INFO) << "Dokan: Mounted";
    mounted = true;
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK Dokan_Unmounted(PDOKAN_FILE_INFO DokanFileInfo)
{
    UNREFERENCED_PARAMETER(DokanFileInfo);

    LOG(LogLevel::INFO) << "Dokan: Unmounted";
    mounted = false;
    return STATUS_SUCCESS;
}

// -----------------------------------------------
#undef ERROR

DOKAN_OPERATIONS dokanOperations = {0};
DOKAN_OPTIONS dokanOptions = {0};
std::string mountpoint;

int StopDokan()
{
    if (fs == NULL) return EXIT_SUCCESS;
    if (mounted == false) return EXIT_SUCCESS;
    LOG(LogLevel::INFO) << "Unmount Dokan mountpoint " << mountpoint;
    if (DokanUnmount(*utf8_to_wstring(mountpoint).data()))
        return EXIT_SUCCESS;
    else
    {
        LOG(LogLevel::ERR) << "Unmount failed";
        return EXIT_FAILURE;
    }
}

int StartDokan(int argc, char *argv[], const char* _mountpoint, CFilesystem &_fs)
{
    fs = &_fs;
    mountpoint = std::string(_mountpoint);

    LOG(LogLevel::INFO) << "Dokan Version: " << DokanVersion();
    LOG(LogLevel::INFO) << "Dokan Driver Version: " << DokanDriverVersion();;

    dokanOptions.Version = DOKAN_VERSION;
    dokanOptions.ThreadCount = 1; // use default = 0 is default
    //dokanOptions.Options |= DOKAN_OPTION_DEBUG;
    //dokanOptions.Options |= DOKAN_OPTION_STDERR;
    //dokanOptions.Options |= DOKAN_OPTION_ALT_STREAM;

    //dokanOptions.Options |= DOKAN_OPTION_NETWORK;
    dokanOptions.Options |= DOKAN_OPTION_REMOVABLE; // we have our own write cache
    //dokanOptions.Options |= DOKAN_OPTION_WRITE_PROTECT;
    //dokanOptions.Options |= DOKAN_OPTION_MOUNT_MANAGER;

    //LPCWSTR
    auto mp = utf8_to_wstring(mountpoint);
    dokanOptions.MountPoint = mp.data();

    dokanOperations.ZwCreateFile         = Dokan_CreateFile;
    dokanOperations.Cleanup              = Dokan_Cleanup;
    dokanOperations.CloseFile            = Dokan_CloseFile;
    dokanOperations.ReadFile             = Dokan_ReadFile;
    dokanOperations.WriteFile            = Dokan_WriteFile;
    dokanOperations.GetFileInformation   = Dokan_GetFileInformation;
    dokanOperations.FindFiles            = Dokan_FindFiles;
    dokanOperations.FindFilesWithPattern = NULL;
    dokanOperations.SetFileAttributes    = Dokan_SetFileAttributes;;
    dokanOperations.DeleteFile           = Dokan_DeleteFile;
    dokanOperations.DeleteDirectory      = Dokan_DeleteDirectory;

    dokanOperations.SetEndOfFile         = Dokan_SetEndOfFile;
    dokanOperations.Mounted              = Dokan_Mounted;
    dokanOperations.Unmounted            = Dokan_Unmounted;
    dokanOperations.GetVolumeInformation = Dokan_GetVolumeInformation;
    dokanOperations.MoveFile             = Dokan_MoveFile;
    dokanOperations.GetDiskFreeSpace     = NULL;
    //dokanOperations.FindStreams        = Dokan_FindStreams;
    //dokanOperations.GetFileSecurity    = Dokan_GetFileSecurity;
    //dokanOperations.LockFile           = Dokan_LockFile;
    dokanOperations.FindFilesWithPattern = NULL;

    int status = DokanMain(&dokanOptions, &dokanOperations);
    switch (status)
    {
    case DOKAN_SUCCESS:
        LOG(LogLevel::INFO) << "Dokan: Success";
        break;
    case DOKAN_ERROR:
        LOG(LogLevel::ERR) << "Dokan: Error";
        return EXIT_FAILURE;
    case DOKAN_DRIVE_LETTER_ERROR:
        LOG(LogLevel::ERR) << "Dokan: Bad Drive letter";
        return EXIT_FAILURE;
    case DOKAN_DRIVER_INSTALL_ERROR:
        LOG(LogLevel::ERR) << "Dokan: Can't install driver";
        return EXIT_FAILURE;
    case DOKAN_START_ERROR:
        LOG(LogLevel::ERR) << "Dokan: Driver something wrong";
        return EXIT_FAILURE;
    case DOKAN_MOUNT_ERROR:
        LOG(LogLevel::ERR) << "Dokan: Can't assign a drive letter";
        return EXIT_FAILURE;
    case DOKAN_MOUNT_POINT_ERROR:
        LOG(LogLevel::ERR) << "Dokan: Mount point error";
        return EXIT_FAILURE;
    case DOKAN_VERSION_ERROR:
        LOG(LogLevel::ERR) << "Dokan: Version error";
        return EXIT_FAILURE;
    default:
        LOG(LogLevel::ERR) << "Dokan: Unknown error: " << status;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
