#include "FileDialogs.h"

#include <Windows.h>
#include <ShObjIdl.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <span>

namespace Nyx::Editor
{
	namespace
	{
		struct DialogOptions
		{
			bool bSave = false;
			const wchar_t* Title = L"";
			std::span<const COMDLG_FILTERSPEC> Filters;
			// Optional: the folder it starts in, the name filled in, and the extension a saved
			// name without one gets (without the dot)
			std::filesystem::path Folder;
			std::wstring Name;
			const wchar_t* DefaultExtension = nullptr;
		};

		// Shows Windows' Open or Save As dialog over the editor's window, which waits for it
		std::optional<std::filesystem::path> ShowFileDialog(const DialogOptions& options)
		{
			// The dialog is a COM object; COM may already be set up on this thread, or not.
			const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			const bool bUninitialize = SUCCEEDED(initialized);

			std::optional<std::filesystem::path> chosen;
			Microsoft::WRL::ComPtr<IFileDialog> dialog;
			const HRESULT created = options.bSave ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
												  : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
			if (SUCCEEDED(created))
			{
				dialog->SetFileTypes(static_cast<UINT>(options.Filters.size()), options.Filters.data());
				dialog->SetTitle(options.Title);
				if (options.DefaultExtension != nullptr)
				{
					dialog->SetDefaultExtension(options.DefaultExtension);
				}
				if (!options.Name.empty())
				{
					dialog->SetFileName(options.Name.c_str());
				}
				Microsoft::WRL::ComPtr<IShellItem> folder;
				if (!options.Folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(options.Folder.wstring().c_str(), nullptr, IID_PPV_ARGS(&folder))))
				{
					dialog->SetFolder(folder.Get());
				}

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

		constexpr COMDLG_FILTERSPEC ThemeFilters[] = {
			{ L"Nyx themes", L"*.nyxtheme" },
			{ L"All files", L"*.*" },
		};
	}

	std::optional<std::filesystem::path> ChooseImageFile()
	{
		const COMDLG_FILTERSPEC filters[] = {
			{ L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.tga" },
			{ L"All files", L"*.*" },
		};
		return ShowFileDialog(DialogOptions{ .Title = L"Choose an image", .Filters = filters });
	}

	std::optional<std::filesystem::path> ChooseInputRecording(const std::filesystem::path& folder)
	{
		constexpr COMDLG_FILTERSPEC filters[] = {
			{ L"Input recordings", L"*.nyxinputrec" },
			{ L"All files", L"*.*" },
		};
		return ShowFileDialog(DialogOptions{ .Title = L"Choose an input recording", .Filters = filters, .Folder = folder });
	}

	std::optional<std::filesystem::path> ChooseThemeFileToOpen(const std::filesystem::path& folder)
	{
		return ShowFileDialog(DialogOptions{ .Title = L"Load a theme", .Filters = ThemeFilters, .Folder = folder });
	}

	std::optional<std::filesystem::path> ChooseThemeFileToSave(const std::filesystem::path& folder, const std::wstring& name)
	{
		return ShowFileDialog(
			DialogOptions{ .bSave = true, .Title = L"Save the theme as", .Filters = ThemeFilters, .Folder = folder, .Name = name, .DefaultExtension = L"nyxtheme" });
	}

	bool ShowInFileExplorer(const std::filesystem::path& path)
	{
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(path, error).make_preferred();
		if (error || !std::filesystem::exists(absolute, error))
		{
			return false;
		}
		if (std::filesystem::is_directory(absolute, error))
		{
			return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"explore", absolute.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
		}

		// A file: its folder, with it selected. The shell's items are COM, which may already be set up
		// on this thread, or not.
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		bool bShown = false;
		if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(absolute.c_str()))
		{
			bShown = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
			ILFree(item);
		}
		if (SUCCEEDED(initialized))
		{
			CoUninitialize();
		}
		return bShown;
	}
}
