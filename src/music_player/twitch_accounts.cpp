#include "twitch_accounts.hpp"

#include <windows.h>
#include <wincred.h>
#include <winhttp.h>
#include <shellapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace winrt::Windows::Data::Json;
namespace {
constexpr const char *kClientId="6h5j0d7kfjaeyw6fejisawwqheeahd";
std::wstring wide(const std::string&v){if(v.empty())return{};int n=MultiByteToWideChar(CP_UTF8,0,v.data(),int(v.size()),nullptr,0);std::wstring o(size_t(n),0);MultiByteToWideChar(CP_UTF8,0,v.data(),int(v.size()),o.data(),n);return o;}
std::string utf8(const std::wstring&v){if(v.empty())return{};int n=WideCharToMultiByte(CP_UTF8,0,v.data(),int(v.size()),nullptr,0,nullptr,nullptr);std::string o(size_t(n),0);WideCharToMultiByte(CP_UTF8,0,v.data(),int(v.size()),o.data(),n,nullptr,nullptr);return o;}
std::string encode(const std::string&v){std::ostringstream o;const char*h="0123456789ABCDEF";for(unsigned char c:v)if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')o<<char(c);else o<<'%'<<h[c>>4]<<h[c&15];return o.str();}
struct Http{int status=0;std::string body;};
Http call(const wchar_t*method,const std::wstring&url,const std::string&token={},const std::string&body={}){Http r;URL_COMPONENTSW p{sizeof(p)};wchar_t host[256]{},path[4096]{};p.lpszHostName=host;p.dwHostNameLength=255;p.lpszUrlPath=path;p.dwUrlPathLength=4095;p.dwSchemeLength=1;if(!WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&p))return r;HINTERNET s=WinHttpOpen(L"RearSilverStreamSuite/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0),c=s?WinHttpConnect(s,std::wstring(host,p.dwHostNameLength).c_str(),p.nPort,0):nullptr;std::wstring target(path,p.dwUrlPathLength);if(p.lpszExtraInfo)target.append(p.lpszExtraInfo,p.dwExtraInfoLength);HINTERNET q=c?WinHttpOpenRequest(c,method,target.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,p.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0):nullptr;std::wstring headers;if(!token.empty())headers=L"Authorization: OAuth "+wide(token)+L"\r\n";if(!body.empty())headers+=L"Content-Type: application/x-www-form-urlencoded\r\n";if(q&&WinHttpSendRequest(q,headers.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:headers.c_str(),DWORD(headers.size()),body.empty()?WINHTTP_NO_REQUEST_DATA:(void*)body.data(),DWORD(body.size()),DWORD(body.size()),0)&&WinHttpReceiveResponse(q,nullptr)){DWORD status=0,size=sizeof(status);WinHttpQueryHeaders(q,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr);r.status=int(status);for(;;){DWORD a=0;if(!WinHttpQueryDataAvailable(q,&a)||!a)break;std::string b(a,0);DWORD n=0;if(!WinHttpReadData(q,b.data(),a,&n))break;b.resize(n);r.body+=b;}}if(q)WinHttpCloseHandle(q);if(c)WinHttpCloseHandle(c);if(s)WinHttpCloseHandle(s);return r;}
std::string field(const JsonObject&o,const wchar_t*k){return o.HasKey(k)?utf8(o.GetNamedString(k,L"").c_str()):std::string{};}
std::wstring folder(){wchar_t p[MAX_PATH]{};GetEnvironmentVariableW(L"APPDATA",p,MAX_PATH);std::wstring f=std::wstring(p)+L"\\RearSilver Stream Suite";CreateDirectoryW(f.c_str(),nullptr);return f;}
void log(const std::string&a,const std::string&m){SYSTEMTIME t{};GetLocalTime(&t);std::ofstream o(folder()+L"\\twitch-diagnostics.log",std::ios::app|std::ios::binary);o<<std::setfill('0')<<std::setw(2)<<t.wHour<<':'<<std::setw(2)<<t.wMinute<<':'<<std::setw(2)<<t.wSecond<<"  "<<a<<": "<<m<<"\r\n";}
}

TwitchAccount::TwitchAccount(std::string a):m_account(std::move(a)){} TwitchAccount::~TwitchAccount(){stop();}
void TwitchAccount::changed(){++m_revision;}
TwitchAccountState TwitchAccount::state()const{std::lock_guard<std::mutex>l(m_mutex);return m_state;}
std::string TwitchAccount::accessToken()const{std::lock_guard<std::mutex>l(m_mutex);return m_accessToken;}
void TwitchAccount::start(){m_stop=false;const bool found=load();{std::lock_guard<std::mutex>l(m_mutex);m_state.authorized=found;m_state.connected=false;}changed();if(found)reconnect();}
void TwitchAccount::stop(){m_stop=true;if(m_worker.joinable())m_worker.join();}
void TwitchAccount::beginLogin(){if(m_worker.joinable())m_worker.join();{std::lock_guard<std::mutex>l(m_mutex);m_state.busy=true;m_state.error.clear();}changed();m_worker=std::thread(&TwitchAccount::loginWorker,this);}
void TwitchAccount::reconnect(){if(m_worker.joinable())m_worker.join();{std::lock_guard<std::mutex>l(m_mutex);if(m_refreshToken.empty()&&m_accessToken.empty()){m_state.error="No saved Twitch login.";changed();return;}m_state.busy=true;m_state.error.clear();}changed();m_worker=std::thread(&TwitchAccount::reconnectWorker,this);}
void TwitchAccount::logout(){if(m_worker.joinable())m_worker.join();{std::lock_guard<std::mutex>l(m_mutex);m_accessToken.clear();m_refreshToken.clear();m_state={};clear();}log(m_account,"Logged out");changed();}
void TwitchAccount::reconnectWorker(){bool ok=validate();if(!ok)ok=refresh()&&validate();{std::lock_guard<std::mutex>l(m_mutex);m_state.busy=false;m_state.authorized=!m_refreshToken.empty();m_state.connected=ok;if(!ok&&m_state.error.empty())m_state.error="Saved Twitch login expired. Log in again.";}changed();}
bool TwitchAccount::validate(){
	std::string token;
	{std::lock_guard<std::mutex>l(m_mutex);token=m_accessToken;}
	if(token.empty())return false;
	auto r=call(L"GET",L"https://id.twitch.tv/oauth2/validate",token);
	log(m_account,"Validate HTTP "+std::to_string(r.status));
	if(r.status!=200)return false;
	JsonObject o;
	if(!JsonObject::TryParse(wide(r.body),o))return false;
	bool chatRead=false,chatEdit=false;
	if(o.HasKey(L"scopes")){
		const auto scopes=o.GetNamedArray(L"scopes",{});
		for(const auto &value:scopes){
			const std::string scope=utf8(value.GetString().c_str());
			chatRead|=scope=="chat:read";
			chatEdit|=scope=="chat:edit";
		}
	}
	if(field(o,L"client_id")!=kClientId||!chatRead||!chatEdit){
		std::lock_guard<std::mutex>l(m_mutex);
		m_state.connected=false;
		m_state.error="The saved Twitch login does not have the required chat permissions. Log in again.";
		log(m_account,"Validation rejected: wrong client or missing chat scopes");
		return false;
	}
	{std::lock_guard<std::mutex>l(m_mutex);m_state.login=field(o,L"login");m_state.userId=field(o,L"user_id");m_state.connected=true;m_state.authorized=true;m_state.error.clear();save();}
	changed();return true;
}
bool TwitchAccount::refresh(){std::string refresh;{std::lock_guard<std::mutex>l(m_mutex);refresh=m_refreshToken;}if(refresh.empty())return false;auto r=call(L"POST",L"https://id.twitch.tv/oauth2/token",{},"client_id="+encode(kClientId)+"&grant_type=refresh_token&refresh_token="+encode(refresh));log(m_account,"Refresh HTTP "+std::to_string(r.status));if(r.status!=200){std::lock_guard<std::mutex>l(m_mutex);if(r.status==400||r.status==401){m_accessToken.clear();m_refreshToken.clear();clear();m_state.authorized=false;}else m_state.error="Could not reach Twitch; reconnect will retry.";return false;}JsonObject o;if(!JsonObject::TryParse(wide(r.body),o))return false;std::lock_guard<std::mutex>l(m_mutex);m_accessToken=field(o,L"access_token");const auto rotated=field(o,L"refresh_token");if(!rotated.empty())m_refreshToken=rotated;save();return!m_accessToken.empty();}
void TwitchAccount::loginWorker(){auto r=call(L"POST",L"https://id.twitch.tv/oauth2/device",{},"client_id="+encode(kClientId)+"&scopes="+encode("chat:read chat:edit"));JsonObject o;if(r.status!=200||!JsonObject::TryParse(wide(r.body),o)){std::lock_guard<std::mutex>l(m_mutex);m_state.busy=false;m_state.error="Twitch login could not be started.";changed();return;}const std::string device=field(o,L"device_code"),verify=field(o,L"verification_uri");const int interval=int(o.GetNamedNumber(L"interval",5)),expires=int(o.GetNamedNumber(L"expires_in",900));ShellExecuteW(nullptr,L"open",wide(verify).c_str(),nullptr,nullptr,SW_SHOWNORMAL);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(expires);bool ok=false;while(!m_stop&&std::chrono::steady_clock::now()<deadline){Sleep(DWORD(interval*1000));auto t=call(L"POST",L"https://id.twitch.tv/oauth2/token",{},"client_id="+encode(kClientId)+"&scopes="+encode("chat:read chat:edit")+"&device_code="+encode(device)+"&grant_type="+encode("urn:ietf:params:oauth:grant-type:device_code"));if(t.status==200){JsonObject x;if(JsonObject::TryParse(wide(t.body),x)){std::lock_guard<std::mutex>l(m_mutex);m_accessToken=field(x,L"access_token");m_refreshToken=field(x,L"refresh_token");save();ok=true;}break;}if(t.status!=400)break;}if(ok)ok=validate();{std::lock_guard<std::mutex>l(m_mutex);m_state.busy=false;m_state.authorized=!m_refreshToken.empty();m_state.connected=ok;if(!ok&&m_state.error.empty())m_state.error="Twitch login was not completed.";}changed();}
bool TwitchAccount::load(){const std::wstring target=L"RearSilver Stream Suite/Twitch "+wide(m_account);PCREDENTIALW c=nullptr;if(!CredReadW(target.c_str(),CRED_TYPE_GENERIC,0,&c))return false;std::string blob((char*)c->CredentialBlob,c->CredentialBlobSize);CredFree(c);std::istringstream in(blob);std::getline(in,m_accessToken);std::getline(in,m_refreshToken);std::getline(in,m_state.login);std::getline(in,m_state.userId);return!m_refreshToken.empty();}
void TwitchAccount::save(){const std::string blob=m_accessToken+'\n'+m_refreshToken+'\n'+m_state.login+'\n'+m_state.userId;const std::wstring target=L"RearSilver Stream Suite/Twitch "+wide(m_account);CREDENTIALW c{};c.Type=CRED_TYPE_GENERIC;c.TargetName=(LPWSTR)target.c_str();c.CredentialBlobSize=DWORD(blob.size());c.CredentialBlob=(LPBYTE)blob.data();c.Persist=CRED_PERSIST_LOCAL_MACHINE;c.UserName=(LPWSTR)L"OAuth";CredWriteW(&c,0);}
void TwitchAccount::clear(){const std::wstring target=L"RearSilver Stream Suite/Twitch "+wide(m_account);CredDeleteW(target.c_str(),CRED_TYPE_GENERIC,0);}
std::string TwitchAccount::diagnostics()const{std::ifstream in(folder()+L"\\twitch-diagnostics.log",std::ios::binary);if(!in)return"No Twitch diagnostic events recorded yet.";std::string s((std::istreambuf_iterator<char>(in)),{});if(s.size()>12000)s=s.substr(s.size()-12000);return s;}
