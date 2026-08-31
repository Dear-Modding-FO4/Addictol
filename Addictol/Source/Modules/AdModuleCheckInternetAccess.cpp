#include <Modules/AdModuleCheckInternetAccess.h>
#include <Core/AdUtils.h>

#include <windows.h>
#include <winhttp.h>

#undef MAX_PATH
#undef MEM_RELEASE

#define AD_NOMESSAGE_CHECKINTERNETACCESS 1

namespace Addictol
{

	namespace detail
	{
		namespace bnet
		{
			namespace ConnectionInfo
			{
				struct Server
				{
					char unk_00[0x20];
					wchar_t hostName[REX::W32::MAX_PATH];
					wchar_t errorMessage[REX::W32::MAX_PATH];
					uint32_t unk440;
					char unk_444[0x24C];
				};

				struct PlatformInfo
				{};
			}

			class HttpConnection
			{
			public:
				static HINTERNET Connect(HttpConnection* _self, ConnectionInfo::Server* serverInfo,
					ConnectionInfo::PlatformInfo* platformInfo) noexcept;
				inline static decltype(&Connect) ConnectOrig{ nullptr };
			};

			HINTERNET HttpConnection::Connect(HttpConnection* _self, ConnectionInfo::Server* serverInfo,
				ConnectionInfo::PlatformInfo* platformInfo) noexcept
			{
				bool bResults = false;
				HINTERNET hSession = nullptr;
				HINTERNET hConnect = nullptr;
				HINTERNET hRequest = nullptr;

				// Obtain a Session Handle
				hSession = WinHttpOpen(L"WinHTTP/1.0",
					WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS, 0);

				// Specify Server
				if (hSession)
					hConnect = WinHttpConnect(hSession, serverInfo->hostName,
						INTERNET_DEFAULT_HTTPS_PORT, 0);

				// Create Request Handle
				if (hConnect)
					hRequest = WinHttpOpenRequest(hConnect, L"HEAD", nullptr,
						nullptr, WINHTTP_NO_REFERER,
						WINHTTP_DEFAULT_ACCEPT_TYPES,
						WINHTTP_FLAG_SECURE);
				
				// Send a Request
				if (hRequest)
					bResults = WinHttpSendRequest(hRequest,
						WINHTTP_NO_ADDITIONAL_HEADERS, 0,
						WINHTTP_NO_REQUEST_DATA, 0,
						0, 0);

				// End the Request
				if (bResults)
					bResults = WinHttpReceiveResponse(hRequest, nullptr);

				// Clean up
				if (hRequest) WinHttpCloseHandle(hRequest);
				if (hConnect) WinHttpCloseHandle(hConnect);
				if (hSession) WinHttpCloseHandle(hSession);

				if (bResults)
				{
#if !AD_NOMESSAGE_CHECKINTERNETACCESS
					REX::INFO(L"bnet::HttpConnection::Connect() access to \"{}\""sv, serverInfo->hostName);
#endif

					return ConnectOrig(_self, serverInfo, platformInfo);
				}
				else
				{
#if !AD_NOMESSAGE_CHECKINTERNETACCESS
					REX::INFO(L"bnet::HttpConnection::Connect() no access to \"{}\""sv, serverInfo->hostName);
#endif

					return nullptr;
				}
			}
		}
	}

	ModuleCheckInternetAccess::ModuleCheckInternetAccess() :
		Module("Check Inernet Access", &bFixesCheckInternetAccess)
	{}

	bool ModuleCheckInternetAccess::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		*(uintptr_t*)(&detail::bnet::HttpConnection::ConnectOrig) =
			RELEX::DetourJump(REL::ID{ 1085707, 2311051 }.address(), (uintptr_t)&detail::bnet::HttpConnection::Connect);

		return true;
	}

}