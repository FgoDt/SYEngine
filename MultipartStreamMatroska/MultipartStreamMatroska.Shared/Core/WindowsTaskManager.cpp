#include "WindowsTaskManager.h"
#include <new>

#if !(WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP))
#include <windows.foundation.h>
#include <windows.storage.h>
#include <windows.storage.streams.h>
#include "AsyncHelper.h"
#include <Shcore.h>
using namespace ABI::Windows::Storage;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Storage::Streams;
#endif

using namespace Downloader::Core;
using namespace Downloader::Windows;

bool WindowsTaskManager::OnStartTask(int index)
{
	Item* file = InternalGetItem(index);
	if (file == NULL || file->Url == NULL)
		return false;
	if (file->Source != NULL)
		return true;
	
	WCHAR szFilePath[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, file->Url, -1, szFilePath, MAX_PATH);
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
	HANDLE hFile = CreateFileW(szFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
#else
	HANDLE hFile = CreateFile2(szFilePath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL);
#endif
	Win32DataSource* src = nullptr;
	if (hFile == INVALID_HANDLE_VALUE) {
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
		return false;
#else
		ComPtr<IStorageFileStatics> storageFileStatics;
		ABI::Windows::Foundation::GetActivationFactory(
			HStringReference(RuntimeClass_Windows_Storage_StorageFile).Get(),
			&storageFileStatics);
		ComPtr<ABI::Windows::Foundation::IAsyncOperation<StorageFile*>> getFileFromPathOperation;
		storageFileStatics->GetFileFromPathAsync(
			HStringReference(szFilePath).Get(),
			&getFileFromPathOperation);
		ComPtr<IStorageFile> file = nullptr;
		AWait(getFileFromPathOperation.Get(), file);
		if (file == nullptr)
			return false;
		ComPtr<ABI::Windows::Foundation::IAsyncOperation<IRandomAccessStream *>> iRandomAccessStreamOperation;
		file->OpenAsync(FileAccessMode_Read, &iRandomAccessStreamOperation);
		ComPtr<IRandomAccessStream> randomAccessStream;
		AWait(iRandomAccessStreamOperation.Get(), randomAccessStream);
		ComPtr<IStream> fileStreamData;
		HRESULT hr = CreateStreamOverRandomAccessStream(
			reinterpret_cast<IUnknown*>(randomAccessStream.Get()), IID_PPV_ARGS(&fileStreamData));
		src = new(std::nothrow) Win32DataSource(fileStreamData);
#endif 
	}
	else
		src = new(std::nothrow) Win32DataSource(hFile);
	if (src == nullptr) {
		CloseHandle(hFile);
		return false;
	}
	file->Source = static_cast<DataSource*>(src);
	return true;
}

bool WindowsTaskManager::OnStopTask(int index)
{
	Item* file = InternalGetItem(index);
	if (file->Source == NULL)
		return true;
	delete file->Source;
	file->Source = NULL;
	return true;
}

void WindowsTaskManager::DestroyTasks() throw()
{
	for (unsigned i = 0; i < GetTaskCapacity(); i++) {
		if (InternalGetItem(i)->Source != NULL)
			StopTask(i);
	}
}

CommonResult WindowsTaskManager::Win32DataSource::InitCheck()
{
	if ((_hFile == INVALID_HANDLE_VALUE || _hFile == NULL) && _stream == nullptr)
		return CommonResult::kNonInit;
	return CommonResult::kSuccess;
}

CommonResult WindowsTaskManager::Win32DataSource::ReadBytes(void* buf, unsigned size, unsigned* read_size)
{
	if (_hFile != NULL) {
		if (buf == NULL ||
			size == 0)
			return CommonResult::kInvalidInput;
		DWORD rs = 0;
		ReadFile(_hFile, buf, size, &rs, NULL);
		if (read_size)
			*read_size = rs;
		return CommonResult::kSuccess;
	}
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP) 
	if (_stream != nullptr) {
		DWORD rs = 0;
		_stream->Read(buf, size, &rs);
		if (read_size)
			*read_size = rs;
		return CommonResult::kSuccess;
	}
#endif
	return CommonResult::kError;
}

CommonResult WindowsTaskManager::Win32DataSource::SetPosition(int64_t offset)
{
	if (_hFile != NULL) {
		LARGE_INTEGER pos;
		pos.QuadPart = offset;
		return SetFilePointerEx(_hFile, pos, &pos, FILE_BEGIN) ? CommonResult::kSuccess : CommonResult::kError;
	}
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
	if (_stream != nullptr)	{
		LARGE_INTEGER sk;
		sk.QuadPart = offset;
		return FAILED(_stream->Seek(sk, STREAM_SEEK_SET, NULL)) ? CommonResult::kError : CommonResult::kSuccess;
	}
#endif
	return CommonResult::kError;
}

CommonResult WindowsTaskManager::Win32DataSource::GetLength(int64_t* size)
{
	if (size == NULL)
		return CommonResult::kInvalidInput;
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
	LARGE_INTEGER s;
	GetFileSizeEx(_hFile, &s);
	*size = s.QuadPart;
#else 
	if (_hFile != NULL) {
		FILE_STANDARD_INFO info = {};
		GetFileInformationByHandleEx(_hFile, FileStandardInfo, &info, sizeof(info));
		*size = info.EndOfFile.QuadPart;
	}
	else if (_stream != nullptr) {
		STATSTG streamStats;
		_stream->Stat(&streamStats, 0);
		*size = streamStats.cbSize.QuadPart;
	}
#endif
	return CommonResult::kSuccess;
}