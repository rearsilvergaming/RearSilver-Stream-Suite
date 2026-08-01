#include "twitch_chat_service.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>

namespace {
std::wstring wide(const std::string &s){if(s.empty())return{};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring w(size_t(n),0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;}
std::string unescape(std::string s){for(size_t p=0;(p=s.find('\\',p))!=std::string::npos;){if(p+1>=s.size())break;char c=s[p+1];std::string r=c=='s'?" ":c==':'?";":c=='r'?"\r":c=='n'?"\n":std::string(1,c);s.replace(p,2,r);p+=r.size();}return s;}
std::map<std::string,std::string> tags(const std::string&s){std::map<std::string,std::string>o;size_t p=0;while(p<s.size()){size_t e=s.find(';',p),q=s.find('=',p);if(q!=std::string::npos&&(e==std::string::npos||q<e))o[s.substr(p,q-p)]=unescape(s.substr(q+1,(e==std::string::npos?s.size():e)-q-1));else o[s.substr(p,(e==std::string::npos?s.size():e)-p)]={};if(e==std::string::npos)break;p=e+1;}return o;}
bool badge(const std::string &b,const char *name){return b.find(std::string(name)+"/")!=std::string::npos;}
}

TwitchChatService::TwitchChatService()=default;
TwitchChatService::~TwitchChatService(){disconnect();}
void TwitchChatService::note(const std::string&m){SYSTEMTIME t{};GetLocalTime(&t);char stamp[16]{};sprintf_s(stamp,"%02u:%02u:%02u",t.wHour,t.wMinute,t.wSecond);std::lock_guard<std::mutex>l(m_mutex);m_diagnostics+=std::string(stamp)+"  "+m+"\r\n";if(m_diagnostics.size()>10000)m_diagnostics.erase(0,m_diagnostics.size()-10000);}
std::string TwitchChatService::diagnostics()const{std::lock_guard<std::mutex>l(m_mutex);return m_diagnostics.empty()?"No Twitch chat events recorded yet.":m_diagnostics;}
void TwitchChatService::connect(std::string login,std::string token,std::string channel,std::string broadcasterId,Handler handler){disconnect();m_stop=false;m_channel=channel;m_thread=std::thread(&TwitchChatService::run,this,std::move(login),std::move(token),std::move(channel),std::move(broadcasterId),std::move(handler));}
void TwitchChatService::disconnect(){m_stop=true;void*s=nullptr;{std::lock_guard<std::mutex>l(m_mutex);s=m_socket;}if(s)WinHttpWebSocketClose((HINTERNET)s,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);if(m_thread.joinable())m_thread.join();m_connected=false;std::lock_guard<std::mutex>l(m_mutex);m_socket=nullptr;}
bool TwitchChatService::sendRaw(const std::string&line){std::lock_guard<std::mutex>l(m_mutex);if(!m_socket)return false;return WinHttpWebSocketSend((HINTERNET)m_socket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,(void*)line.data(),DWORD(line.size()))==NO_ERROR;}
bool TwitchChatService::sendMessage(const std::string&message){if(!m_connected||m_channel.empty())return false;std::string clean=message;clean.erase(std::remove(clean.begin(),clean.end(),'\r'),clean.end());std::replace(clean.begin(),clean.end(),'\n',' ');if(clean.size()>430)clean.resize(430);return sendRaw("PRIVMSG #"+m_channel+" :"+clean+"\r\n");}
void TwitchChatService::run(std::string login,std::string token,std::string channel,std::string broadcasterId,Handler handler){
	for(int attempt=0;!m_stop;++attempt){
		HINTERNET session=WinHttpOpen(L"RearSilverStreamSuite/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0);
		HINTERNET connection=session?WinHttpConnect(session,L"irc-ws.chat.twitch.tv",443,0):nullptr;
		HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",L"/",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
		if(request)WinHttpSetOption(request,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0);
		HINTERNET socket=nullptr;
		if(request&&WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr))socket=WinHttpWebSocketCompleteUpgrade(request,0);
		if(request)WinHttpCloseHandle(request);
		if(!socket){note("IRC WebSocket connection failed");if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);for(int i=0;i<20&&!m_stop;++i)Sleep(100);continue;}
		{std::lock_guard<std::mutex>l(m_mutex);m_socket=socket;}m_connected=true;
		sendRaw("PASS oauth:"+token+"\r\n");sendRaw("NICK "+login+"\r\n");sendRaw("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n");sendRaw("JOIN #"+channel+"\r\n");note("IRC connected as "+login+" to #"+channel);
		std::string pending;char buffer[8192];
		while(!m_stop){DWORD read=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE type;DWORD e=WinHttpWebSocketReceive(socket,buffer,sizeof(buffer),&read,&type);if(e!=NO_ERROR||type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)break;pending.append(buffer,read);if(type==WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE||type==WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE)continue;size_t end=0;while((end=pending.find("\r\n"))!=std::string::npos){std::string line=pending.substr(0,end);pending.erase(0,end+2);if(line.rfind("PING",0)==0){sendRaw("PONG"+line.substr(4)+"\r\n");continue;}size_t priv=line.find(" PRIVMSG #");if(priv==std::string::npos||line.empty()||line[0]!='@'||!handler)continue;size_t tagEnd=line.find(' '),textPos=line.find(" :",priv);if(tagEnd==std::string::npos||textPos==std::string::npos)continue;auto t=tags(line.substr(1,tagEnd-1));TwitchChatMessage m;m.userId=t["user-id"];m.displayName=t["display-name"];m.text=line.substr(textPos+2);m.subscriber=t["subscriber"]=="1"||badge(t["badges"],"subscriber")||badge(t["badges"],"founder");m.vip=badge(t["badges"],"vip");m.moderator=t["mod"]=="1"||badge(t["badges"],"moderator");m.broadcaster=(!broadcasterId.empty()&&m.userId==broadcasterId)||badge(t["badges"],"broadcaster");handler(m);}}
		m_connected=false;{std::lock_guard<std::mutex>l(m_mutex);m_socket=nullptr;}WinHttpWebSocketClose(socket,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);WinHttpCloseHandle(socket);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);if(!m_stop){note("IRC disconnected; retrying");for(int i=0;i<std::min(100,10+attempt*10)&&!m_stop;++i)Sleep(100);}
	}
}
