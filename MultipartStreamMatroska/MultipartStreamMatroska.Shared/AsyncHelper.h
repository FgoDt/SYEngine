#pragma once
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <Windows.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

template <typename TResult, typename TResult2>
inline void AWait(ABI::Windows::Foundation::IAsyncOperation<TResult>* asyncOp, TResult2& result)
{
	HANDLE handle = CreateEventEx(NULL, NULL, 0, EVENT_ALL_ACCESS); 
	asyncOp->put_Completed(Microsoft::WRL::Callback<ABI::Windows::Foundation::IAsyncOperationCompletedHandler<TResult>>([&]
	(ABI::Windows::Foundation::IAsyncOperation<TResult>* asyncInfo, ABI::Windows::Foundation::AsyncStatus asyncStatus)
	{
		auto hr = asyncInfo->GetResults(&result);
		if (FAILED(hr))
			result = nullptr;
		SetEvent(handle);
		return S_OK;
	}).Get());
	WaitForSingleObjectEx(handle, INFINITE, FALSE);
}