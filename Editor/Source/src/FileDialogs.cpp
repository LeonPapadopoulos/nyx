#include "FileDialogs.h"

#include <Windows.h>
#include <ShObjIdl.h>
#include <wrl/client.h>

namespace Nyx::Editor
{
	std::optional<std::filesystem::path> ChooseImageFile()
	{
		// The dialog is a COM object; COM may already be set up on this thread, or not.
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		const bool bUninitialize = SUCCEEDED(initialized);

		std::optional<std::filesystem::path> chosen;
		Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
		{
			const COMDLG_FILTERSPEC filters[] = {
				{ L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.tga" },
				{ L"All files", L"*.*" },
			};
			dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
			dialog->SetTitle(L"Choose an image");

			// Over the editor's window, which waits for it
			Microsoft::WRL::ComPtr<IShellItem> item;
			if (SUCCEEDED(dialog->Show(GetActiveWindow())) && SUCCEEDED(dialog->GetResult(&item)))
			{
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
				{
					chosen = std::filesystem::path(path);
					CoTaskMemFree(path);
				}
			}
		}

		if (bUninitialize)
		{
			CoUninitialize();
		}
		return chosen;
	}
}
