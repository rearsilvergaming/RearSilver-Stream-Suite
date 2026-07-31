#include "spotify_client.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincred.h>
#include <winhttp.h>
#include <shellapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

using namespace winrt::Windows::Data::Json;

namespace {
constexpr const wchar_t *kRedirectUri = L"http://127.0.0.1:18246/spotify/callback";
constexpr const wchar_t *kCredentialTarget = L"RearSilver Stream Suite/Spotify Refresh Token";
constexpr const wchar_t *kRegistryPath = L"Software\\RearSilver\\RearSilver-Stream-Suite\\music";
constexpr const wchar_t *kClientIdValue = L"spotifyClientId";

std::wstring dataFolder(){wchar_t value[MAX_PATH]{};GetEnvironmentVariableW(L"APPDATA",value,MAX_PATH);std::wstring folder=std::wstring(value)+L"\\RearSilver Stream Suite";CreateDirectoryW(folder.c_str(),nullptr);return folder;}
std::wstring authFile(){return dataFolder()+L"\\spotify-auth.dat";}
std::wstring logFile(){return dataFolder()+L"\\spotify-diagnostics.log";}
void spotifyLog(const std::string &message){SYSTEMTIME t{};GetLocalTime(&t);std::ofstream out(logFile(),std::ios::app|std::ios::binary);out<<std::setfill('0')<<std::setw(2)<<t.wHour<<':'<<std::setw(2)<<t.wMinute<<':'<<std::setw(2)<<t.wSecond<<"  "<<message<<"\r\n";OutputDebugStringA(("[RS Spotify] "+message+"\n").c_str());}

std::wstring wide(const std::string &value) {
	if (value.empty()) return {}; int n=MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),nullptr,0);
	std::wstring out(size_t(n),L'\0'); MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),out.data(),n); return out;
}
std::string utf8(const std::wstring &value) {
	if(value.empty())return{};int n=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
	std::string out(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),out.data(),n,nullptr,nullptr);return out;
}
std::string randomText(size_t length) {
	static constexpr char chars[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";
	std::random_device rd; std::uniform_int_distribution<size_t>d(0,sizeof(chars)-2);std::string out;out.reserve(length);
	for(size_t i=0;i<length;++i)out.push_back(chars[d(rd)]);return out;
}
std::string base64Url(const unsigned char *data,size_t size) {
	DWORD chars=0;CryptBinaryToStringA(data,DWORD(size),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&chars);
	std::string out(chars,'\0');CryptBinaryToStringA(data,DWORD(size),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,out.data(),&chars);
	out.resize(chars);while(!out.empty()&&(out.back()=='='||out.back()=='\0'))out.pop_back();
	std::replace(out.begin(),out.end(),'+','-');std::replace(out.begin(),out.end(),'/','_');return out;
}
std::string challenge(const std::string &verifier) {
	BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};DWORD objectSize=0,bytes=0;std::vector<unsigned char>object,digest(32);
	if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return{};
	BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&bytes,0);object.resize(objectSize);
	if(BCryptCreateHash(algorithm,&hash,object.data(),DWORD(object.size()),nullptr,0,0)>=0){BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(verifier.data())),ULONG(verifier.size()),0);BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0);}
	if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return base64Url(digest.data(),digest.size());
}
std::string urlEncode(const std::string &value) {
	std::ostringstream out;const char hex[]="0123456789ABCDEF";for(unsigned char c:value){if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out<<char(c);else out<<'%'<<hex[c>>4]<<hex[c&15];}return out.str();
}
std::string queryValue(const std::string &query,const std::string &name) {
	const std::string needle=name+'=';size_t p=query.find(needle);if(p==std::string::npos)return{};p+=needle.size();size_t e=query.find('&',p);std::string value=query.substr(p,e-p);
	std::string out;for(size_t i=0;i<value.size();++i){if(value[i]=='%'&&i+2<value.size()){out.push_back(char(std::strtoul(value.substr(i+1,2).c_str(),nullptr,16)));i+=2;}else if(value[i]=='+')out.push_back(' ');else out.push_back(value[i]);}return out;
}
struct HttpResult{int status=0;std::string body;};
HttpResult request(const wchar_t *method,const std::wstring &url,const std::string &bearer={},const std::string &body={}) {
	HttpResult result;URL_COMPONENTSW parts{sizeof(parts)};wchar_t host[256]{},path[4096]{};parts.lpszHostName=host;parts.dwHostNameLength=255;parts.lpszUrlPath=path;parts.dwUrlPathLength=4095;parts.dwSchemeLength=1;
	if(!WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&parts))return result;HINTERNET session=WinHttpOpen(L"RearSilverStreamSuite/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0);if(!session)return result;
	HINTERNET connection=WinHttpConnect(session,std::wstring(host,parts.dwHostNameLength).c_str(),parts.nPort,0);std::wstring target(path,parts.dwUrlPathLength);if(parts.lpszExtraInfo)target.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
	HINTERNET call=WinHttpOpenRequest(connection,method,target.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);
	std::wstring headers;if(!bearer.empty())headers=L"Authorization: Bearer "+wide(bearer)+L"\r\n";if(!body.empty())headers+=L"Content-Type: application/x-www-form-urlencoded\r\n";
	if(WinHttpSendRequest(call,headers.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:headers.c_str(),DWORD(headers.size()),body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),DWORD(body.size()),DWORD(body.size()),0)&&WinHttpReceiveResponse(call,nullptr)){
		DWORD status=0,size=sizeof(status);WinHttpQueryHeaders(call,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr);result.status=int(status);
		for(;;){DWORD available=0;if(!WinHttpQueryDataAvailable(call,&available)||!available)break;std::string chunk(available,'\0');DWORD read=0;if(!WinHttpReadData(call,chunk.data(),available,&read))break;chunk.resize(read);result.body+=chunk;}
	}if(call)WinHttpCloseHandle(call);if(connection)WinHttpCloseHandle(connection);WinHttpCloseHandle(session);return result;
}
int64_t nowSeconds(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string jsonString(const JsonObject&o,const wchar_t*key){return o.HasKey(key)?utf8(o.GetNamedString(key,L"").c_str()):std::string{};}
SpotifyQueueTrack parseTrack(const IJsonValue &value){SpotifyQueueTrack t;if(!value||value.ValueType()!=JsonValueType::Object)return t;auto o=value.GetObject();t.uri=jsonString(o,L"uri");t.title=jsonString(o,L"name");t.durationMs=int64_t(o.GetNamedNumber(L"duration_ms",0));if(o.HasKey(L"artists")){auto a=o.GetNamedArray(L"artists");for(uint32_t i=0;i<a.Size();++i){if(i)t.artist+=", ";t.artist+=jsonString(a.GetObjectAt(i),L"name");}}if(o.HasKey(L"album")){auto a=o.GetNamedObject(L"album");t.album=jsonString(a,L"name");if(a.HasKey(L"images")){auto images=a.GetNamedArray(L"images");if(images.Size())t.artworkUrl=jsonString(images.GetObjectAt(0),L"url");}}return t;}
}

SpotifyClient::SpotifyClient()=default; SpotifyClient::~SpotifyClient(){stop();}
void SpotifyClient::start(){m_stop=false;const bool hasRefresh=loadCredentials();spotifyLog(std::string("Client started; Client ID ")+(m_clientId.empty()?"missing":"loaded")+", refresh token "+(hasRefresh?"loaded":"missing"));{std::lock_guard<std::mutex>lock(m_mutex);m_state.clientId=m_clientId;m_state.authorized=hasRefresh;m_state.connected=hasRefresh;}if(!m_actionThread.joinable())m_actionThread=std::thread(&SpotifyClient::actionWorker,this);if(!m_queueThread.joinable())m_queueThread=std::thread(&SpotifyClient::queueWorker,this);}
void SpotifyClient::stop(){m_stop=true;m_actionReady.notify_all();if(m_loginThread.joinable())m_loginThread.join();if(m_actionThread.joinable())m_actionThread.join();if(m_queueThread.joinable())m_queueThread.join();}
SpotifyClientState SpotifyClient::state()const{std::lock_guard<std::mutex>lock(m_mutex);return m_state;}
void SpotifyClient::beginLogin(){if(m_loginThread.joinable())m_loginThread.join();{std::lock_guard<std::mutex>lock(m_mutex);if(m_clientId.empty()){m_state.error="Enter your Spotify Client ID first.";return;}m_state.busy=true;m_state.error.clear();}m_loginThread=std::thread(&SpotifyClient::loginWorker,this);}
void SpotifyClient::logout(){std::lock_guard<std::mutex>operationLock(m_operationMutex);std::lock_guard<std::mutex>lock(m_mutex);m_accessToken.clear();m_refreshToken.clear();m_expiresAt=0;m_state={};m_state.clientId=m_clientId;clearCredentials();saveCredentials();spotifyLog("Spotify disconnected; Client ID retained");}
void SpotifyClient::refreshQueue(){loadQueue();}
void SpotifyClient::refreshQueueAsync(){enqueue([this]{refreshQueue();});}
void SpotifyClient::setClientIdAsync(std::string clientId){enqueue([this,clientId=std::move(clientId)]{setClientId(clientId);});}
void SpotifyClient::logoutAsync(){enqueue([this]{logout();});}
void SpotifyClient::setClientId(const std::string &clientId){std::string cleaned;for(unsigned char c:clientId)if(!std::isspace(c))cleaned.push_back(char(c));std::lock_guard<std::mutex>operationLock(m_operationMutex);std::lock_guard<std::mutex>lock(m_mutex);if(!cleaned.empty()&&(cleaned.size()!=32||!std::all_of(cleaned.begin(),cleaned.end(),[](unsigned char c){return std::isxdigit(c)!=0;}))){m_state.error="Spotify Client IDs contain exactly 32 letters and numbers.";spotifyLog("Rejected invalid Client ID");return;}if(cleaned==m_clientId){m_state.clientId=m_clientId;m_state.error.clear();spotifyLog("Client ID unchanged");return;}m_clientId=cleaned;m_accessToken.clear();m_refreshToken.clear();m_expiresAt=0;m_state={};m_state.clientId=m_clientId;clearCredentials();saveCredentials();spotifyLog(std::string("Client ID saved: ")+(m_clientId.empty()?"no":"yes"));}
std::string SpotifyClient::clientId()const{std::lock_guard<std::mutex>lock(m_mutex);return m_clientId;}
std::string SpotifyClient::diagnostics()const{std::ifstream in(logFile(),std::ios::binary);if(!in)return"No Spotify diagnostic events recorded yet.";std::string text((std::istreambuf_iterator<char>(in)),{});if(text.size()>12000)text=text.substr(text.size()-12000);return text;}
void SpotifyClient::loginWorker(){
	const std::string verifier=randomText(80),csrf=randomText(32),pkce=challenge(verifier);
	std::string clientId;{std::lock_guard<std::mutex>lock(m_mutex);clientId=m_clientId;}
	WSADATA wsa{};WSAStartup(MAKEWORD(2,2),&wsa);SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(18246);if(listener==INVALID_SOCKET||bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==SOCKET_ERROR||listen(listener,1)==SOCKET_ERROR){if(listener!=INVALID_SOCKET)closesocket(listener);WSACleanup();std::lock_guard<std::mutex>lock(m_mutex);m_state.busy=false;m_state.connected=false;m_state.error="Spotify callback port 18246 is unavailable. Close any other Suite Media Player and try again.";return;}
	const std::string scopes="user-read-playback-state user-modify-playback-state";
	const std::string auth="https://accounts.spotify.com/authorize?client_id="+urlEncode(clientId)+"&response_type=code&redirect_uri="+urlEncode(utf8(kRedirectUri))+"&code_challenge_method=S256&code_challenge="+pkce+"&state="+csrf+"&scope="+urlEncode(scopes);
	ShellExecuteW(nullptr,L"open",wide(auth).c_str(),nullptr,nullptr,SW_SHOWNORMAL);
	std::string code,error;bool accepted=false;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
	while(!m_stop&&std::chrono::steady_clock::now()<deadline&&!accepted){fd_set set;FD_ZERO(&set);FD_SET(listener,&set);timeval timeout{1,0};if(select(0,&set,nullptr,nullptr,&timeout)>0){accepted=true;SOCKET client=accept(listener,nullptr,nullptr);char buffer[8192]{};int n=recv(client,buffer,sizeof(buffer)-1,0);std::string requestText(buffer,n>0?n:0);size_t first=requestText.find(' '),second=requestText.find(' ',first+1);std::string target=(first!=std::string::npos&&second!=std::string::npos)?requestText.substr(first+1,second-first-1):std::string{};size_t question=target.find('?');std::string query=question==std::string::npos?std::string{}:target.substr(question+1);code=queryValue(query,"code");error=queryValue(query,"error");if(queryValue(query,"state")!=csrf){code.clear();error="Security state mismatch";}const std::string html=code.empty()?"<h2>Spotify authorisation failed</h2><p>Return to the Media Player for details.</p>":"<h2>Spotify authorisation received</h2><p>The Media Player is finishing the secure connection. You may close this window.</p>";std::string response="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "+std::to_string(html.size())+"\r\nConnection: close\r\n\r\n"+html;send(client,response.data(),int(response.size()),0);closesocket(client);}}
	if(!accepted&&!m_stop)error="Spotify login timed out";closesocket(listener);WSACleanup();spotifyLog(code.empty()?"OAuth callback contained no code":"OAuth callback code received");
	bool ok=!code.empty()&&exchangeCode(code,verifier);if(ok)ok=loadProfile();{std::lock_guard<std::mutex>lock(m_mutex);m_state.busy=false;m_state.connected=ok;m_state.error=ok?"":(error.empty()?"Spotify login failed":error);}if(ok)loadQueue();
}
bool SpotifyClient::exchangeCode(const std::string&code,const std::string&verifier){std::string clientId;{std::lock_guard<std::mutex>lock(m_mutex);clientId=m_clientId;}std::string body="client_id="+urlEncode(clientId)+"&grant_type=authorization_code&code="+urlEncode(code)+"&redirect_uri="+urlEncode(utf8(kRedirectUri))+"&code_verifier="+urlEncode(verifier);auto r=request(L"POST",L"https://accounts.spotify.com/api/token",{},body);spotifyLog("Token exchange HTTP "+std::to_string(r.status));if(r.status!=200){std::lock_guard<std::mutex>lock(m_mutex);m_state.error="Spotify token exchange failed (HTTP "+std::to_string(r.status)+"). Check the Client ID and exact redirect URI.";return false;}JsonObject o;if(!JsonObject::TryParse(wide(r.body),o))return false;std::lock_guard<std::mutex>lock(m_mutex);m_accessToken=jsonString(o,L"access_token");m_refreshToken=jsonString(o,L"refresh_token");m_expiresAt=nowSeconds()+int64_t(o.GetNamedNumber(L"expires_in",3600));m_state.clientId=m_clientId;m_state.authorized=!m_refreshToken.empty();saveCredentials();return!m_accessToken.empty();}
bool SpotifyClient::refreshAccessToken(){std::string refresh,clientId;{std::lock_guard<std::mutex>lock(m_mutex);refresh=m_refreshToken;clientId=m_clientId;}if(refresh.empty()||clientId.empty())return false;std::string body="client_id="+urlEncode(clientId)+"&grant_type=refresh_token&refresh_token="+urlEncode(refresh);auto r=request(L"POST",L"https://accounts.spotify.com/api/token",{},body);spotifyLog("Token refresh HTTP "+std::to_string(r.status));if(r.status!=200){std::lock_guard<std::mutex>lock(m_mutex);m_state.clientId=m_clientId;m_state.authorized=!m_refreshToken.empty();m_state.connected=m_state.authorized;if(r.status==400||r.status==401){m_accessToken.clear();m_refreshToken.clear();m_expiresAt=0;m_state.authorized=false;m_state.connected=false;m_state.error="Spotify authorisation expired. Connect Spotify again.";saveCredentials();}else m_state.error="Spotify is temporarily unavailable (HTTP "+std::to_string(r.status)+").";return false;}JsonObject o;if(!JsonObject::TryParse(wide(r.body),o))return false;std::lock_guard<std::mutex>lock(m_mutex);m_accessToken=jsonString(o,L"access_token");const auto rotated=jsonString(o,L"refresh_token");if(!rotated.empty())m_refreshToken=rotated;m_expiresAt=nowSeconds()+int64_t(o.GetNamedNumber(L"expires_in",3600));m_state.clientId=m_clientId;m_state.authorized=!m_refreshToken.empty();m_state.connected=m_state.authorized;m_state.error.clear();saveCredentials();return!m_accessToken.empty();}

bool SpotifyClient::addToQueue(const std::string &uri){std::lock_guard<std::mutex>operationLock(m_operationMutex);if(uri.empty())return false;std::string token;int64_t expiry;{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;expiry=m_expiresAt;}if((token.empty()||expiry<=nowSeconds()+60)&&!refreshAccessToken())return false;{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;}const std::wstring url=std::wstring(L"https://api.spotify.com/v1/me/player/queue?uri=")+wide(urlEncode(uri));auto r=request(L"POST",url,token);if(r.status==401&&refreshAccessToken()){{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;}r=request(L"POST",url,token);}spotifyLog("Add to queue HTTP "+std::to_string(r.status));if(r.status==204)return true;std::lock_guard<std::mutex>lock(m_mutex);m_state.error="Spotify could not add the track to its queue (HTTP "+std::to_string(r.status)+").";return false;}

bool SpotifyClient::toggleShuffle(){std::lock_guard<std::mutex>operationLock(m_operationMutex);std::string token;int64_t expiry;{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;expiry=m_expiresAt;}if((token.empty()||expiry<=nowSeconds()+60)&&!refreshAccessToken())return false;const bool desired=!m_shuffleEnabled;{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;}const std::wstring url=std::wstring(L"https://api.spotify.com/v1/me/player/shuffle?state=")+(desired?L"true":L"false");auto r=request(L"PUT",url,token);if(r.status==401&&refreshAccessToken()){{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;}r=request(L"PUT",url,token);}spotifyLog("Shuffle HTTP "+std::to_string(r.status));if(r.status==204){m_shuffleEnabled=desired;return true;}std::lock_guard<std::mutex>lock(m_mutex);m_state.error="Spotify could not change shuffle (HTTP "+std::to_string(r.status)+").";return false;}
bool SpotifyClient::loadProfile(){std::string token;{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;}auto r=request(L"GET",L"https://api.spotify.com/v1/me",token);if(r.status!=200)return false;JsonObject o;if(!JsonObject::TryParse(wide(r.body),o))return false;std::lock_guard<std::mutex>lock(m_mutex);m_state.displayName=jsonString(o,L"display_name");saveCredentials();return true;}
bool SpotifyClient::loadQueue(){
	std::lock_guard<std::mutex> operationLock(m_operationMutex);
	std::string token;int64_t expiry;{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;expiry=m_expiresAt;}
	if((token.empty()||expiry<=nowSeconds()+60)&&!refreshAccessToken()){spotifyLog("Queue refresh could not obtain an access token");return false;}
	{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;}
	auto r=request(L"GET",L"https://api.spotify.com/v1/me/player/queue",token);
	if(r.status==401&&refreshAccessToken()){{std::lock_guard<std::mutex>lock(m_mutex);token=m_accessToken;}r=request(L"GET",L"https://api.spotify.com/v1/me/player/queue",token);}
	if(r.status!=200){spotifyLog("Queue request failed with HTTP "+std::to_string(r.status)+" (body "+std::to_string(r.body.size())+" bytes)");std::lock_guard<std::mutex>lock(m_mutex);m_state.error="Spotify queue request failed (HTTP "+std::to_string(r.status)+").";return false;}
	JsonObject o;if(!JsonObject::TryParse(wide(r.body),o)){spotifyLog("Queue HTTP 200 contained invalid JSON ("+std::to_string(r.body.size())+" bytes)");return false;}
	SpotifyQueueTrack current;std::vector<SpotifyQueueTrack>queue;uint32_t apiQueueSize=0;
	const bool hasCurrent=o.HasKey(L"currently_playing")&&o.GetNamedValue(L"currently_playing").ValueType()==JsonValueType::Object;
	if(hasCurrent)current=parseTrack(o.GetNamedValue(L"currently_playing"));
	const bool hasQueue=o.HasKey(L"queue")&&o.GetNamedValue(L"queue").ValueType()==JsonValueType::Array;
	if(hasQueue){auto a=o.GetNamedArray(L"queue");apiQueueSize=a.Size();for(uint32_t i=0;i<a.Size();++i){auto t=parseTrack(a.GetAt(i));if(!t.uri.empty())queue.push_back(std::move(t));}}
	if(queue.size()!=m_lastLoggedQueueCount){spotifyLog("Queue response: HTTP 200, "+std::to_string(r.body.size())+" bytes, current="+(hasCurrent?"track":"none")+", API items="+std::to_string(apiQueueSize)+", parsed="+std::to_string(queue.size()));m_lastLoggedQueueCount=queue.size();}
	std::lock_guard<std::mutex>lock(m_mutex);m_state.clientId=m_clientId;m_state.authorized=!m_refreshToken.empty();m_state.current=std::move(current);m_state.queue=std::move(queue);m_state.connected=true;m_state.queueChecked=true;m_state.playbackAvailable=hasCurrent||apiQueueSize>0;m_state.error.clear();return true;
}
void SpotifyClient::queueWorker(){while(!m_stop){SpotifyClientState s=state();if(s.authorized)loadQueue();for(int i=0;i<30&&!m_stop;++i)Sleep(100);}}
void SpotifyClient::enqueue(std::function<void()> action){{std::lock_guard<std::mutex>lock(m_actionMutex);m_actions.push_back(std::move(action));}m_actionReady.notify_one();}
void SpotifyClient::actionWorker(){for(;;){std::function<void()>action;{std::unique_lock<std::mutex>lock(m_actionMutex);m_actionReady.wait(lock,[this]{return m_stop||!m_actions.empty();});if(m_stop&&m_actions.empty())break;action=std::move(m_actions.front());m_actions.pop_front();}if(action)action();}}
bool SpotifyClient::loadCredentials(){std::string id,refresh;std::ifstream in(authFile(),std::ios::binary);if(in){uint32_t idSize=0,blobSize=0;in.read(reinterpret_cast<char*>(&idSize),sizeof(idSize));if(idSize<=128){id.resize(idSize);in.read(id.data(),idSize);in.read(reinterpret_cast<char*>(&blobSize),sizeof(blobSize));if(blobSize<=16384){std::vector<BYTE>encrypted(blobSize);if(blobSize)in.read(reinterpret_cast<char*>(encrypted.data()),blobSize);if(blobSize){DATA_BLOB input{blobSize,encrypted.data()},output{};if(CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,0,&output)){refresh.assign(reinterpret_cast<char*>(output.pbData),output.cbData);LocalFree(output.pbData);}else spotifyLog("DPAPI could not decrypt saved refresh token; error "+std::to_string(GetLastError()));}}}}if(id.empty()){wchar_t legacy[128]{};DWORD bytes=sizeof(legacy);if(RegGetValueW(HKEY_CURRENT_USER,kRegistryPath,kClientIdValue,RRF_RT_REG_SZ,nullptr,legacy,&bytes)==ERROR_SUCCESS){id=utf8(legacy);spotifyLog("Migrated Client ID from the previous registry store");}}std::lock_guard<std::mutex>lock(m_mutex);m_clientId=id;m_refreshToken=refresh;m_accessToken.clear();m_expiresAt=0;m_state.clientId=m_clientId;m_state.authorized=!m_refreshToken.empty();if(!m_clientId.empty()&&!in)saveCredentials();return!m_clientId.empty()&&!m_refreshToken.empty();}
void SpotifyClient::saveCredentials(){std::vector<BYTE> encrypted;if(!m_refreshToken.empty()){DATA_BLOB input{DWORD(m_refreshToken.size()),reinterpret_cast<BYTE*>(m_refreshToken.data())},output{};if(CryptProtectData(&input,L"RearSilver Stream Suite Spotify",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)){encrypted.assign(output.pbData,output.pbData+output.cbData);LocalFree(output.pbData);}else{m_state.error="Windows could not encrypt the Spotify refresh token (error "+std::to_string(GetLastError())+").";spotifyLog(m_state.error);return;}}const std::wstring temp=authFile()+L".tmp";std::ofstream out(temp,std::ios::binary|std::ios::trunc);const uint32_t idSize=uint32_t(m_clientId.size()),blobSize=uint32_t(encrypted.size());out.write(reinterpret_cast<const char*>(&idSize),sizeof(idSize));out.write(m_clientId.data(),idSize);out.write(reinterpret_cast<const char*>(&blobSize),sizeof(blobSize));if(blobSize)out.write(reinterpret_cast<const char*>(encrypted.data()),blobSize);out.close();if(!MoveFileExW(temp.c_str(),authFile().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){m_state.error="Windows could not save Spotify settings (error "+std::to_string(GetLastError())+").";spotifyLog(m_state.error);}else spotifyLog("Spotify settings persisted; Client ID "+std::string(m_clientId.empty()?"missing":"present")+", refresh token "+(m_refreshToken.empty()?"missing":"present"));}
void SpotifyClient::clearCredentials(){DeleteFileW(authFile().c_str());CredDeleteW(kCredentialTarget,CRED_TYPE_GENERIC,0);}
