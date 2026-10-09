#include "SourceNavigation.h"
#include "EditorBuildConfig.h"

#include <Windows.h>
#include <Shellapi.h>
#include <comdef.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

namespace Nyx::Editor
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		_variant_t Invoke(IDispatch* object, const wchar_t* name, WORD flags,
			std::initializer_list<_variant_t> arguments = {})
		{
			DISPID id = 0;
			LPOLESTR member = const_cast<LPOLESTR>(name);
			_com_util::CheckError(object->GetIDsOfNames(IID_NULL, &member, 1, LOCALE_USER_DEFAULT, &id));
			// Automation expects arguments in reverse order. Their storage stays owned by arguments.
			std::vector<VARIANT> reversed;
			for (auto it = std::rbegin(arguments); it != std::rend(arguments); ++it) reversed.push_back(*it);
			DISPID put = DISPID_PROPERTYPUT;
			DISPPARAMS parameters{};
			parameters.rgvarg = reversed.data();
			parameters.cArgs = static_cast<UINT>(reversed.size());
			if (flags & DISPATCH_PROPERTYPUT)
			{
				parameters.rgdispidNamedArgs = &put;
				parameters.cNamedArgs = 1;
			}
			_variant_t result;
			_com_util::CheckError(object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags,
				&parameters, &result, nullptr, nullptr));
			return result;
		}

		ComPtr<IDispatch> GetObject(IDispatch* object, const wchar_t* name)
		{
			_variant_t value = Invoke(object, name, DISPATCH_PROPERTYGET);
			if (value.vt != VT_DISPATCH || !value.pdispVal) _com_issue_error(E_NOINTERFACE);
			return ComPtr<IDispatch>(value.pdispVal);
		}

		ComPtr<IDispatch> FindVisualStudio(const std::filesystem::path& solutionPath)
		{
			ComPtr<IRunningObjectTable> table;
			_com_util::CheckError(GetRunningObjectTable(0, &table));
			ComPtr<IEnumMoniker> entries;
			_com_util::CheckError(table->EnumRunning(&entries));
			ComPtr<IBindCtx> binding;
			_com_util::CheckError(CreateBindCtx(0, &binding));
			ComPtr<IMoniker> entry;
			while (entries->Next(1, &entry, nullptr) == S_OK)
			{
				LPOLESTR displayName = nullptr;
				if (SUCCEEDED(entry->GetDisplayName(binding.Get(), nullptr, &displayName)))
				{
					const bool isVisualStudio = std::wstring_view(displayName).starts_with(L"!VisualStudio.DTE.");
					CoTaskMemFree(displayName);
					if (isVisualStudio)
					{
						try
						{
							ComPtr<IUnknown> running;
							ComPtr<IDispatch> dte;
							_com_util::CheckError(table->GetObject(entry.Get(), &running));
							_com_util::CheckError(running.As(&dte));
							const auto solution = GetObject(dte.Get(), L"Solution");
							_variant_t name = Invoke(solution.Get(), L"FullName", DISPATCH_PROPERTYGET);
							std::error_code error;
							if (name.vt == VT_BSTR && name.bstrVal &&
								std::filesystem::equivalent(name.bstrVal, solutionPath, error) && !error) return dte;
						}
						catch (const _com_error& error)
						{
							// Do not open another IDE just because the running one has a modal dialog.
							if (error.Error() == RPC_E_CALL_REJECTED || error.Error() == RPC_E_SERVERCALL_RETRYLATER) throw;
						}
					}
				}
				entry.Reset();
			}
			return {};
		}

		void OpenInVisualStudio(const std::filesystem::path& file, uint32_t line)
		{
			const std::filesystem::path solutionPath(SolutionPath);
			ComPtr<IDispatch> dte = FindVisualStudio(solutionPath);
			if (!dte)
			{
				CLSID classId{};
				_com_util::CheckError(CLSIDFromProgID(L"VisualStudio.DTE", &classId));
				_com_util::CheckError(CoCreateInstance(classId, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&dte)));
				if (std::filesystem::is_regular_file(solutionPath))
				{
					const auto solution = GetObject(dte.Get(), L"Solution");
					Invoke(solution.Get(), L"Open", DISPATCH_METHOD, { _variant_t(solutionPath.c_str()) });
				}
			}
			Invoke(dte.Get(), L"UserControl", DISPATCH_PROPERTYPUT, { _variant_t(true) });
			const auto window = GetObject(dte.Get(), L"MainWindow");
			Invoke(window.Get(), L"Visible", DISPATCH_PROPERTYPUT, { _variant_t(true) });
			const auto operations = GetObject(dte.Get(), L"ItemOperations");
			Invoke(operations.Get(), L"OpenFile", DISPATCH_METHOD, { _variant_t(file.c_str()), _variant_t(L"{00000000-0000-0000-0000-000000000000}") });
			const auto document = GetObject(dte.Get(), L"ActiveDocument");
			const auto selection = GetObject(document.Get(), L"Selection");
			Invoke(selection.Get(), L"GotoLine", DISPATCH_METHOD, { _variant_t(static_cast<long>(line)), _variant_t(true) });
			Invoke(window.Get(), L"Activate", DISPATCH_METHOD);
		}
	}

	std::wstring MakeVSCodeSourceUri(const std::filesystem::path& file, uint32_t line)
	{
		const auto utf8 = file.generic_u8string();
		constexpr wchar_t hex[] = L"0123456789ABCDEF";
		std::wstring uri = L"vscode://file/";
		for (unsigned char byte : utf8)
		{
			if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
				(byte >= '0' && byte <= '9') || byte == '/' || byte == ':' || byte == '-' || byte == '_' || byte == '.' || byte == '~')
			{
				uri += static_cast<wchar_t>(byte);
			}
			else
			{
				uri += L'%';
				uri += hex[byte >> 4];
				uri += hex[byte & 15];
			}
		}
		return uri + L":" + std::to_wstring(line);
	}

	std::string OpenSourceLocation(SourceLocation location, ESourceEditor editor)
	{
		if (!location.IsValid()) return "This element has no source location.";
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(initialized)) return "Could not initialize source editor integration.";
		std::string error;
		try
		{
			std::filesystem::path file = std::filesystem::u8path(location.File);
			if (file.is_relative()) file = std::filesystem::path(SourceRoot) / file;
			if (!std::filesystem::is_regular_file(file))
			{
				error = "Source file was not found. Rebuild Nyx from this checkout: " + file.string();
			}
			else if (editor == ESourceEditor::VisualStudioCode)
			{
				const auto uri = MakeVSCodeSourceUri(file, location.Line);
				if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
					error = "Could not open Visual Studio Code. Check its installation and URL handler.";
			}
			else OpenInVisualStudio(file, location.Line);
		}
		catch (const _com_error& failure)
		{
			error = "Visual Studio could not open the source location (HRESULT " +
				std::to_string(static_cast<unsigned long>(failure.Error())) + "). Check that Visual Studio is installed and has no modal dialog open.";
		}
		catch (const std::exception& failure) { error = failure.what(); }
		CoUninitialize();
		return error;
	}
}
