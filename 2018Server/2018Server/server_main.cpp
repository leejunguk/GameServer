#define WIN32_LEAN_AND_MEAN  
#define INITGUID

#include <WinSock2.h>
#include <windows.h>   // include important windows stuff

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "lua53.lib")

#include "protocol.h"
#include <thread>
#include <vector>
#include <array>
#include <iostream>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <queue>
#include <string>
using namespace std;
using namespace chrono;

#define UNICODE  
#include <sqlext.h>  
#include <locale.h>

#define NAME_LEN 50  
#define LEVEL_LEN 20  

extern "C" {
#include "include/lua.h"
#include "include/lauxlib.h"
#include "include/lualib.h"
}

int CAPI_send_chat_packet(lua_State *L);
int CAPI_get_x_position(lua_State *L);
int CAPI_get_y_position(lua_State *L);
int CAPI_get_HP(lua_State *L);
int CAPI_send_HP_packet(lua_State *L);

HANDLE gh_iocp;

static const int EVT_RECV = 0;
static const int EVT_SEND = 1;
static const int EVT_MOVE = 2;
static const int EVT_PLAYER_MOVE = 3;

struct EXOVER {
	WSAOVERLAPPED m_over;
	char m_iobuf[MAX_BUFF_SIZE];
	WSABUF m_wsabuf;
	char event_type;
	int target_object;
};

class Client
{
public:
	int m_x;
	int m_y;
	int HP;
	int m_GAMEID;
	bool m_isactive;
	lua_State *L;
	SOCKET m_s;
	bool m_isconnected;
	unordered_set <int> m_viewlist;
	mutex m_mvl;

	EXOVER m_rxover;
	int m_packet_size;  // 지금 조립하고 있는 패킷의 크기
	int	m_prev_packet_size; // 지난번 recv에서 완성되지 않아서 저장해 놓은 패킷의 앞부분의 크기
	char m_packet[MAX_PACKET_SIZE];

	Client()
	{
		m_isconnected = false;
		m_x = 4;
		m_y = 4;
		HP = 100;

		ZeroMemory(&m_rxover.m_over, sizeof(WSAOVERLAPPED));
		m_rxover.m_wsabuf.buf = m_rxover.m_iobuf;
		m_rxover.m_wsabuf.len = sizeof(m_rxover.m_wsabuf.buf);
		m_rxover.event_type = EVT_RECV;
		m_prev_packet_size = 0;
	}
};

array <Client, NUM_OF_NPC> g_clients;
int IDarray[400];
int g_DBX;
int g_DBY;
void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER  iError;
	WCHAR       wszMessage[1000];
	WCHAR       wszState[SQL_SQLSTATE_SIZE + 1];


	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT *)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

void show_error()
{
	printf("error\n");
	//HandleDiagnosticRecord();
}



void DB_main()
{
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLWCHAR szName[NAME_LEN];
	SQLINTEGER nCHAR_LEVEL, nID, nPositionX, nPositionY;
	SQLLEN cbName = 0, cbID = 0, cbCHAR_LEVEL = 0, cbPositionX, cbPositionY;



	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2018_GAME_WT", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)L"EXEC search_DB 100", SQL_NTS);
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &nID, 100, &cbID);
						retcode = SQLBindCol(hstmt, 2, SQL_C_CHAR, szName, NAME_LEN, &cbName);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &nCHAR_LEVEL, LEVEL_LEN, &cbCHAR_LEVEL);
						retcode = SQLBindCol(hstmt, 4, SQL_INTEGER, &nPositionX, LEVEL_LEN, &cbPositionX);
						retcode = SQLBindCol(hstmt, 5, SQL_INTEGER, &nPositionY, LEVEL_LEN, &cbPositionY);

						// Fetch and print each row of data. On an error, display a message and exit.  
						for (int i = 0; ; i++) {
							retcode = SQLFetch(hstmt);
							if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
								HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);//show_error();
							if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
							{
								wprintf(L"%d: %d %S %d\n", i + 1, nID, szName, nCHAR_LEVEL);
								IDarray[i + 1] = nID;
							}
							else
								break;
						}
					}
					else
					{
						HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
	//return false;
}

void DB_update(int gameid, int x, int y)
{
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLWCHAR szName[NAME_LEN];
	SQLINTEGER nCHAR_LEVEL, nID, nPositionX, nPositionY;
	SQLLEN cbName = 0, cbID = 0, cbCHAR_LEVEL = 0, cbPositionX, cbPositionY;



	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2018_GAME_WT", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					int tmpid = gameid;
					wstring id = to_wstring(tmpid);
					int tmpx = x;
					wstring positionx = to_wstring(tmpx);
					int tmpy = y;
					wstring positiony = to_wstring(tmpy);

					wstring query = L"EXEC updatepos ";
					query += to_wstring(x);
					query += L",";
					query += to_wstring(y);
					query += L",";
					query += to_wstring(gameid);
					query += L"\0";
					retcode = SQLExecDirect(hstmt, (SQLWCHAR*)&query, SQL_NTS);
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &nID, 100, &cbID);
						retcode = SQLBindCol(hstmt, 2, SQL_C_CHAR, szName, NAME_LEN, &cbName);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &nCHAR_LEVEL, LEVEL_LEN, &cbCHAR_LEVEL);
						retcode = SQLBindCol(hstmt, 4, SQL_INTEGER, &nPositionX, LEVEL_LEN, &cbPositionX);
						retcode = SQLBindCol(hstmt, 5, SQL_INTEGER, &nPositionY, LEVEL_LEN, &cbPositionY);

						g_DBX = nPositionX;
						g_DBY = nPositionY;

						// Fetch and print each row of data. On an error, display a message and exit.  
						//for (int i = 0; ; i++) {
						//	retcode = SQLFetch(hstmt);
						//	if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
						//		HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);//show_error();
						//	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
						//	{
						//		wprintf(L"%d: %d %S %d\n", i + 1, nID, szName, nCHAR_LEVEL);
						//		IDarray[i + 1] = nID;
						//	}
						//	else
						//		break;
						//}
					}
					else
					{
						HandleDiagnosticRecord(hstmt, SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
	//return false;
}
bool DB_search(int a)
{
	for (int i = 0; i < 400; ++i)
	{
		if (IDarray[i] == a)
			return true;
	}
	return false;
}
struct EVENT {
	int obj_id;
	high_resolution_clock::time_point wakeup_t;
	int event_type;
	int target_id;
};

class compare_c {
	bool reserver;
public:
	bool operator() (const EVENT lhs, const EVENT rhs) const
	{
		return (lhs.wakeup_t > rhs.wakeup_t);
	}
};

priority_queue <EVENT, vector<EVENT>, compare_c> timer_queue;

void add_timer(int obj_id, int event_type, high_resolution_clock::time_point wakeup_t)
{
	timer_queue.push(EVENT{ obj_id, wakeup_t, event_type, 0 });
}

void ProcessEvent(EVENT &ev)
{
	EXOVER *over = new EXOVER;
	over->event_type = ev.event_type;
	PostQueuedCompletionStatus(gh_iocp, 1, ev.obj_id, &over->m_over);
}

void WakeUpNPC(int npc_id)
{
	// if (true == CAS(&m_isactive, false, true)) add_timer();
	if (false == g_clients[npc_id].m_isactive) {
		g_clients[npc_id].m_isactive = true;
		add_timer(npc_id, EVT_MOVE, high_resolution_clock::now() + 1s);
	}
}

void timer_thread()
{
	while (true) {
		this_thread::sleep_for(10us);
		while (false == timer_queue.empty()) {
			if (timer_queue.top().wakeup_t > high_resolution_clock::now())
				break;
			EVENT ev = timer_queue.top();
			timer_queue.pop();
			ProcessEvent(ev);
		}
	}
}

void error_display(const char *msg, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
		std::cout << msg;
		std::wcout << L"  에러" << lpMsgBuf << std::endl;
		LocalFree(lpMsgBuf);
		while (true);
}

void ErrorDisplay(const char * location)
{
	error_display(location, WSAGetLastError());
}

bool IsNPC(int id)
{
	return ((id >= NPC_START) && (id < NUM_OF_NPC));
}

bool CanSee(int a, int b)
{
	int dist_x = g_clients[a].m_x - g_clients[b].m_x;
	int dist_y = g_clients[a].m_y - g_clients[b].m_y;
	int dist = dist_x * dist_x + dist_y * dist_y;
	return (VIEW_RADIUS * VIEW_RADIUS >= dist);
}

void initialize()
{
	for (int i = NPC_START; i < NUM_OF_NPC; ++i) {
		g_clients[i].m_x = rand() % BOARD_WIDTH;
		g_clients[i].m_y = rand() % BOARD_HEIGHT;
		g_clients[i].m_isactive = false;

		lua_State *L = luaL_newstate();
		luaL_openlibs(L);
		luaL_loadfile(L, "bluedragon.lua");
		lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_myid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 0, 0);

		lua_register(L, "API_send_chat_packet", CAPI_send_chat_packet);
		lua_register(L, "API_get_x_position", CAPI_get_x_position);
		lua_register(L, "API_get_y_position", CAPI_get_y_position);

		//Lua확장 
		lua_register(L, "API_get_HP", CAPI_get_HP);
		lua_register(L, "API_send_HP_packet", CAPI_send_HP_packet);
		

		g_clients[i].L = L;
	}

	cout << "VM loading completed!\n";

	gh_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0); // 의미없는 파라메터, 마지막은 알아서 쓰레드를 만들어준다.
	std::wcout.imbue(std::locale("korean"));

	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
}

void StartRecv(int id)
{
	unsigned long r_flag = 0;
	ZeroMemory(&g_clients[id].m_rxover.m_over, sizeof(WSAOVERLAPPED));
	int ret = WSARecv(g_clients[id].m_s, &g_clients[id].m_rxover.m_wsabuf, 1, 
		NULL, &r_flag, &g_clients[id].m_rxover.m_over, NULL);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no) error_display("Recv Error", err_no);
	}
}

void SendPacket(int id, void *ptr)
{
	unsigned char *packet = reinterpret_cast<unsigned char *>(ptr);
	EXOVER *s_over = new EXOVER;
	s_over->event_type = EVT_SEND;
	memcpy(s_over->m_iobuf, packet, packet[0]);
	s_over->m_wsabuf.buf = s_over->m_iobuf;
	s_over->m_wsabuf.len = packet[0];
	ZeroMemory(&s_over->m_over, sizeof(WSAOVERLAPPED));
	int res = WSASend(g_clients[id].m_s, &s_over->m_wsabuf, 1, NULL, 0,
		&s_over->m_over, NULL);
	if (0 != res) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no) error_display("Send Error! ", err_no);
	}
}

void SendPutObjectPacket(int client, int object)
{
	sc_packet_put_player p;
	p.id = object;
	p.size = sizeof(p);
	p.type = SC_PUT_PLAYER;
	p.x = g_clients[object].m_x;
	p.y = g_clients[object].m_y;
	p.HP = g_clients[object].HP; //확장프로토콜 추가 
	SendPacket(client, &p);
}

void SendAttackObjectPacket(int client, int object,int damage)
{
	sc_packet_put_player p;
	p.id = object;
	p.size = sizeof(p);
	p.type = SC_PUT_PLAYER;
	p.x = g_clients[object].m_x;
	p.y = g_clients[object].m_y;
	p.HP = g_clients[object].HP - damage; //확장프로토콜 추가 
	SendPacket(client, &p);
}


void SendRemoveObjectPacket(int client, int object)
{
	sc_packet_remove_player p;
	p.id = object;
	p.size = sizeof(p);
	p.type = SC_REMOVE_PLAYER;
	SendPacket(client, &p);
}

void SendChatPacket(int client, int speaker, WCHAR *mess)
{
	sc_packet_chat p;
	p.id = speaker;
	p.size = sizeof(p);
	p.type = SC_CHAT;
	wcscpy_s(p.message, mess);
	SendPacket(client, &p);
}

int CAPI_send_chat_packet(lua_State *L)
{
	int client = lua_tonumber(L, -3);
	int speaker = lua_tonumber(L, -2);
	char *mess = (char *)lua_tostring(L, -1);
	lua_pop(L, 4);

	size_t len = strlen(mess);
	if (len > MAX_STR_SIZE - 1) len = MAX_STR_SIZE - 1;
	size_t wlen = 0;
	WCHAR wmess[MAX_STR_SIZE + MAX_STR_SIZE];
	mbstowcs_s(&wlen, wmess, len, mess, _TRUNCATE);
	wmess[MAX_STR_SIZE - 1] = 0;

	SendChatPacket(client, speaker, wmess);
	return 0;
}
int CAPI_send_HP_packet(lua_State *L)
{
	int damaged = lua_tonumber(L, -2);
	int attacker = lua_tonumber(L, -1);
	lua_pop(L, 3);

	SendAttackObjectPacket(damaged, attacker, 5);
	//SendRemoveObjectPacket(damaged,attacker);
	return 0;
}

int CAPI_get_x_position(lua_State *L)
{
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);
	int x = g_clients[obj_id].m_x;
	lua_pushnumber(L, x);
	return 1;
}

int CAPI_get_y_position(lua_State *L)
{
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);
	int y = g_clients[obj_id].m_y;
	lua_pushnumber(L, y);
	return 1;
}

int CAPI_get_HP(lua_State *L)
{
	int obj_id = lua_tonumber(L, -1);
	lua_pop(L, 2);
	int hp = g_clients[obj_id].HP;
	lua_pushnumber(L,hp);
	return 1;

}


void ProcessPacket(int id, char *packet)
{
	int x = g_clients[id].m_x;
	int y = g_clients[id].m_y;
	int hp = g_clients[id].HP;
	switch (packet[1])
	{
	case CS_UP: if (y > 0) y--; break;
	case CS_DOWN: if (y < BOARD_HEIGHT - 1) y++; break;
	case CS_LEFT: if (x > 0) x--; break;
	case CS_RIGHT: if (x < BOARD_WIDTH - 1) x++; break;
	case SC_PUT_PLAYER: 
	{
		//cout << " 우연" << endl;
		sc_packet_put_player *my_packet = reinterpret_cast<sc_packet_put_player *>(packet);

		//g_clients[id] =
		g_clients[id].m_GAMEID = my_packet->id;

		DB_main();
		if (true == DB_search(g_clients[id].m_GAMEID) || IsNPC(id))
		{
			g_clients[id].m_x = g_DBX;
			g_clients[id].m_y = g_DBY;
		}
		else
		{
			closesocket(g_clients[id].m_s);
		}
	}
	default: 
		cout << "Unkown Packet Type from Client [" << id << "]\n";
		return;
	}
	//DB_main();
	//if (true == DB_search(g_clients[id].m_GAMEID) || IsNPC(id))
	//{
	//	g_clients[id].m_x = g_DBX;
	//	g_clients[id].m_y = g_DBY;
	//}

	g_clients[id].m_x = x;
	g_clients[id].m_y = y;
	g_clients[id].HP = hp;

	if (!IsNPC(id))
	{
		DB_update(g_clients[id].m_GAMEID, x, y);
	}
	


	sc_packet_pos pos_packet;

	pos_packet.id = id;
	pos_packet.size = sizeof(sc_packet_pos);
	pos_packet.type = SC_POS;
	pos_packet.x = x;
	pos_packet.y = y;
	pos_packet.HP = g_clients[id].HP;

	unordered_set<int> new_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (i == id) continue;
		if (false == g_clients[i].m_isconnected) continue;
		if (true == CanSee(id, i)) new_vl.insert(i);
	}
	for (int i = NPC_START; i < NUM_OF_NPC; ++i) {
		if (true == CanSee(id, i)) {
			new_vl.insert(i);
			EXOVER *exover = new EXOVER;
			exover->event_type = EVT_PLAYER_MOVE;
			exover->target_object = id;
			PostQueuedCompletionStatus(gh_iocp, 1, i, &exover->m_over);
		}
	}

	SendPacket(id, &pos_packet);

	// new_vl에는 있는데 old_vl에 없는 경우
	for (auto ob : new_vl) {
		g_clients[id].m_mvl.lock();
		if (0 == g_clients[id].m_viewlist.count(ob)) {
			g_clients[id].m_viewlist.insert(ob);
			g_clients[id].m_mvl.unlock();
			
			SendPutObjectPacket(id, ob);

			if (true == IsNPC(ob)) { 
				WakeUpNPC(ob);
				continue;
			}

			g_clients[ob].m_mvl.lock();
			if (0 == g_clients[ob].m_viewlist.count(id)) {
				g_clients[ob].m_viewlist.insert(id);
				g_clients[ob].m_mvl.unlock();
				SendPutObjectPacket(ob, id);
			} else {
				g_clients[ob].m_mvl.unlock();
				SendPacket(ob, &pos_packet);
			}
		}
		else {
	// new_vl에도 있고 old_vl에도 있는 경우
			g_clients[id].m_mvl.unlock();
			if (true == IsNPC(ob)) continue;
			g_clients[ob].m_mvl.lock();
			if (0 != g_clients[ob].m_viewlist.count(id)) {
				g_clients[ob].m_mvl.unlock();
				SendPacket(ob, &pos_packet);
			}
			else {
				g_clients[ob].m_viewlist.insert(id);
				g_clients[ob].m_mvl.unlock();
				SendPutObjectPacket(ob, id);
			}
		}
	}

	// new_vl에는 없는데 old_vl에 있는 경우
	vector <int> to_remove;
	g_clients[id].m_mvl.lock();
	unordered_set<int> vl_copy = g_clients[id].m_viewlist;
	g_clients[id].m_mvl.unlock();
	for (auto ob : vl_copy) {
		if (0 == new_vl.count(ob)) {
			to_remove.push_back(ob);

			if (true == IsNPC(ob)) continue;
			g_clients[ob].m_mvl.lock();
			if (0 != g_clients[ob].m_viewlist.count(id)) {
				g_clients[ob].m_viewlist.erase(id);
				g_clients[ob].m_mvl.unlock();
				SendRemoveObjectPacket(ob, id);
			}
			else {
				g_clients[ob].m_mvl.unlock();
			}
		}
	}

	g_clients[id].m_mvl.lock();
	for (auto ob : to_remove) g_clients[id].m_viewlist.erase(ob);
	g_clients[id].m_mvl.unlock();
	for (auto ob : to_remove) {
		SendRemoveObjectPacket(id, ob);
	}
}

void DisconnectPlayer(int id)
{
	sc_packet_remove_player p;
	p.id = id;
	p.size = sizeof(p);
	p.type = SC_REMOVE_PLAYER;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == g_clients[i].m_isconnected) continue;
		if (i == id) continue;

		if (true == IsNPC(i)) break;
		g_clients[i].m_mvl.lock();
		if (0 != g_clients[i].m_viewlist.count(id)) {
			g_clients[i].m_viewlist.erase(id);
			g_clients[i].m_mvl.unlock();
			SendPacket(i, &p);
		}
		else {
			g_clients[i].m_mvl.unlock();
		}
	}
	closesocket(g_clients[id].m_s);
	g_clients[id].m_mvl.lock();
	g_clients[id].m_viewlist.clear();
	g_clients[id].m_mvl.unlock();
	g_clients[id].m_isconnected = false;
}

void MoveNPC(int i)
{
	unordered_set <int> old_vl;
	for (int id = 0; id < MAX_USER; ++id) {
		if (false == g_clients[id].m_isconnected) continue;
		if (CanSee(id, i)) old_vl.insert(id);
	}
	switch (rand() % 4) {
	case 0: if (g_clients[i].m_x > 0) g_clients[i].m_x--; break;
	case 1: if (g_clients[i].m_x < BOARD_WIDTH - 1) g_clients[i].m_x++; break;
	case 2: if (g_clients[i].m_y < BOARD_HEIGHT - 1) g_clients[i].m_y++; break;
	case 3: if (g_clients[i].m_y > 0) g_clients[i].m_y--; break;
	}
	unordered_set <int> new_vl;
	for (int id = 0; id < MAX_USER; ++id) {
		if (false == g_clients[id].m_isconnected) continue;
		if (CanSee(id, i)) new_vl.insert(id);
	}
	//mutex pos_lock;
	//pos_lock.lock();
	sc_packet_pos pos_p;
	pos_p.id = i;
	pos_p.size = sizeof(pos_p);
	pos_p.type = SC_POS;
	pos_p.x = g_clients[i].m_x;
	pos_p.y = g_clients[i].m_y;
	g_clients[i].HP = g_clients[i].HP - 10;
	pos_p.HP = g_clients[i].HP;

	//SendPacket(i, &pos_p);

	// 멀어진 플레이어에서 시야 삭제
	for (auto id : old_vl) {
		if (0 == new_vl.count(id)) {
			g_clients[id].m_mvl.lock();
			if (g_clients[id].m_viewlist.count(i)) {
				g_clients[id].m_viewlist.erase(i);
				g_clients[id].m_mvl.unlock();
				SendRemoveObjectPacket(id, i);
			}
			else {
				g_clients[id].m_mvl.unlock();
			}
		}
		else {
			// 계속 보고 있다.
			g_clients[id].m_mvl.lock();
			if (0 != g_clients[id].m_viewlist.count(i)) {
				g_clients[id].m_mvl.unlock();
			//	pos_lock.unlock();//추가
				if (g_clients[id].HP < 0 || 100 < g_clients[id].HP)
				{
					SendRemoveObjectPacket(id, i);
				}
				else
				SendPacket(id, &pos_p);
			}
			else {
				g_clients[id].m_viewlist.insert(i);
				g_clients[id].m_mvl.unlock();
			//	pos_lock.unlock();//추가
				if (g_clients[id].HP < 0 || 100 < g_clients[id].HP)
				{
					SendRemoveObjectPacket(id, i);
				}
				else
				SendPutObjectPacket(id, i);
			}
		}
	}
	// 새로 만난 플레이어에게 시야 추가
	for (auto id : new_vl) {
		if (0 == old_vl.count(id)) {
			g_clients[id].m_mvl.lock();
			if (0 == g_clients[id].m_viewlist.count(i)) {
				g_clients[id].m_viewlist.insert(i);
				g_clients[id].m_mvl.unlock();
			//	pos_lock.unlock();
				if (g_clients[id].HP < 0 || 100 < g_clients[id].HP)
				{
					SendRemoveObjectPacket(id, i);
				}
				else
				 SendPutObjectPacket(id, i);
			}
			else {
				g_clients[id].m_mvl.unlock();
			//	pos_lock.unlock();
				if (g_clients[id].HP < 0 || 100 < g_clients[id].HP)
				{
					SendRemoveObjectPacket(id, i);
				}
				else
				SendPacket(id, &pos_p);
			}
		}
	}

	if (false == new_vl.empty())
		add_timer(i, EVT_MOVE, high_resolution_clock::now() + 1s);
	else
		g_clients[i].m_isactive = false;
}

void worker_thread()
{
	while (true)
	{
		unsigned long io_size;
		unsigned long long iocp_key; // 64 비트 integer , 우리가 64비트로 컴파일해서 64비트
		WSAOVERLAPPED *over;
		BOOL ret = GetQueuedCompletionStatus(gh_iocp, &io_size, &iocp_key, &over, INFINITE);
		int key = static_cast<int>(iocp_key);
//		cout << "WT::Network I/O with Client [" << key << "]\n";
		if (FALSE == ret) {
			cout << "Error in GQCS\n";
			DisconnectPlayer(key);
			continue;
		}
		if (0 == io_size) {
			DisconnectPlayer(key);
			continue;
		}

		EXOVER *p_over = reinterpret_cast<EXOVER *>(over);
		if (EVT_RECV == p_over->event_type ) {
//			cout << "WT:Packet From Client [" << key << "]\n";
			int work_size = io_size;
			char *wptr = p_over->m_iobuf;
			while (0 < work_size) {
				int p_size;
				if (0 != g_clients[key].m_packet_size)
					p_size = g_clients[key].m_packet_size;
				else {
					p_size = wptr[0];
					g_clients[key].m_packet_size = p_size;
				}
				int need_size = p_size - g_clients[key].m_prev_packet_size;
				if (need_size <= work_size) {
					memcpy(g_clients[key].m_packet 
						+ g_clients[key].m_prev_packet_size, wptr, need_size);
					ProcessPacket(key, g_clients[key].m_packet);
					g_clients[key].m_prev_packet_size = 0;
					g_clients[key].m_packet_size = 0;
					work_size -= need_size;
					wptr += need_size;
				}
				else {
					memcpy(g_clients[key].m_packet + g_clients[key].m_prev_packet_size, wptr, work_size);
					g_clients[key].m_prev_packet_size += work_size;
					work_size = -work_size;
					wptr += work_size;
				}
			}
			StartRecv(key);
		}
		else if (EVT_SEND == p_over->event_type) {  // Send 후처리
//			cout << "WT:A packet was sent to Client[" << key << "]\n";
			delete p_over;
		}
		else if (EVT_MOVE == p_over->event_type) {
			MoveNPC(static_cast<int>(iocp_key));
		}
		else if (EVT_PLAYER_MOVE == p_over->event_type) {
			int player_id = p_over->target_object;
			lua_State *L = g_clients[key].L;
			lua_getglobal(L, "player_move");
			lua_pushnumber(L, player_id);
			lua_pushnumber(L, g_clients[player_id].m_x);
			lua_pushnumber(L, g_clients[player_id].m_y);
			lua_pcall(L, 3, 0, 0);
			delete p_over;
		} else {
			cout << "Unknown Event Type detected in worker thread!!\n";
		}
	}
}

void accept_thread()	//새로 접속해 오는 클라이언트를 IOCP로 넘기는 역할
{
	SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN bind_addr;
	ZeroMemory(&bind_addr, sizeof(SOCKADDR_IN));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(MY_SERVER_PORT);
	bind_addr.sin_addr.s_addr = INADDR_ANY;	// 0.0.0.0  아무대서나 오는 것을 다 받겠다.

	::bind(s, reinterpret_cast<sockaddr *>(&bind_addr), sizeof(bind_addr));
	listen(s, 1000);

	while (true)
	{
		SOCKADDR_IN c_addr;
		ZeroMemory(&c_addr, sizeof(SOCKADDR_IN));
		c_addr.sin_family = AF_INET;
		c_addr.sin_port = htons(MY_SERVER_PORT);
		c_addr.sin_addr.s_addr = INADDR_ANY;	// 0.0.0.0  아무대서나 오는 것을 다 받겠다.
		int addr_size = sizeof(sockaddr);

		SOCKET cs = WSAAccept(s, reinterpret_cast<sockaddr *>(&c_addr), &addr_size, NULL, NULL);
		if (INVALID_SOCKET == cs) {
			ErrorDisplay("In Accept Thread:WSAAccept()");
			continue;
		}
//		cout << "New Client Connected!\n";
		int id = -1;
		for (int i = 0; i < MAX_USER; ++i) 
			if (false == g_clients[i].m_isconnected) {
				id = i;
				break;
			}
		if (-1 == id) {
			cout << "MAX USER Exceeded\n";
			continue;
		}
//		cout << "ID of new Client is [" << id << "]";
		g_clients[id].m_s = cs;
		g_clients[id].m_packet_size = 0;
		g_clients[id].m_prev_packet_size = 0;
		g_clients[id].m_viewlist.clear();
		g_clients[id].m_x = 4;
		g_clients[id].m_y = 4;
		//g_clients[id].HP = 100; // 확장 프로토콜 HP 초기화 

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(cs), gh_iocp, id, 0);
		g_clients[id].m_isconnected = true;
		StartRecv(id);

		sc_packet_put_player p;
		p.id = id;
		p.size = sizeof(p);
		p.type = SC_PUT_PLAYER;
		p.x = g_clients[id].m_x;
		p.y = g_clients[id].m_y;
		p.HP = g_clients[id].HP; // 확장프로토콜 추가 

		SendPacket(id, &p);

		// 나의 접속을 기존 플레이어들에게 알려준다.
		for (int i = 0;i<MAX_USER;++i)
			if (true == g_clients[i].m_isconnected) {
				if (false == CanSee(i, id)) continue;
				if (i == id) continue;
				g_clients[i].m_mvl.lock();
				g_clients[i].m_viewlist.insert(id);
				g_clients[i].m_mvl.unlock();
				SendPacket(i, &p);
			}

		// 나에게 이미 접속해 있는 플레이어들의 정보를 알려준다.
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == g_clients[i].m_isconnected) continue;
			if (i == id) continue;
			if (false == CanSee(i, id)) continue;
			p.id = i;
			p.x = g_clients[i].m_x;
			p.y = g_clients[i].m_y;
			p.HP = g_clients[i].HP; //프로토콜 확장
			g_clients[id].m_mvl.lock();
			g_clients[id].m_viewlist.insert(i);
			g_clients[id].m_mvl.unlock();
			SendPacket(id, &p);
		}
		// 주위에 있는 NPC 정보를 알려 준다.
		for (int i = NPC_START; i < NUM_OF_NPC; ++i) {
			if (false == CanSee(i, id)) continue;
			p.id = i;
			p.x = g_clients[i].m_x;
			p.y = g_clients[i].m_y;
			p.HP = g_clients[i].HP;
			g_clients[id].m_mvl.lock();
			g_clients[id].m_viewlist.insert(i);
			g_clients[id].m_mvl.unlock();
			WakeUpNPC(i);
			SendPacket(id, &p);
		}
	}
}

void NPC_ai_thread()
{
	while (true) {
		Sleep(1000);
		for (int i = NPC_START; i < NUM_OF_NPC; ++i) {
			unordered_set <int> old_vl;
			for (int id = 0; id < MAX_USER; ++id) {
				if (false == g_clients[id].m_isconnected) continue;
				if (CanSee(id, i)) old_vl.insert(id);
			}
			switch (rand() % 4) {
			case 0: if (g_clients[i].m_x > 0) g_clients[i].m_x--; break;
			case 1: if (g_clients[i].m_x < BOARD_WIDTH - 1) g_clients[i].m_x++; break;
			case 2: if (g_clients[i].m_y < BOARD_HEIGHT - 1) g_clients[i].m_y++; break;
			case 3: if (g_clients[i].m_y > 0) g_clients[i].m_y--; break;
			}
			unordered_set <int> new_vl;
			for (int id = 0; id < MAX_USER; ++id) {
				if (false == g_clients[id].m_isconnected) continue;
				if (CanSee(id, i)) new_vl.insert(id);
			}

			//mutex pos_lock;
			//pos_lock.lock();
			sc_packet_pos pos_p;
			pos_p.id = i;
			pos_p.size = sizeof(pos_p);
			pos_p.type = SC_POS;
			pos_p.x = g_clients[i].m_x;
			pos_p.y = g_clients[i].m_y;
			pos_p.HP = g_clients[i].HP;

			// 멀어진 플레이어에서 시야 삭제
			for (auto id : old_vl) {
				if (0 == new_vl.count(i)) {
					g_clients[id].m_mvl.lock();
					if (g_clients[id].m_viewlist.count(i)) {
						g_clients[id].m_viewlist.erase(i);
						g_clients[id].m_mvl.unlock();
						//pos_lock.unlock();
						SendRemoveObjectPacket(id, i);
					}
					else {
						g_clients[id].m_mvl.unlock();
					}
				} else {
					// 계속 보고 있다.
					g_clients[id].m_mvl.lock();
					if (0 != g_clients[id].m_viewlist.count(i)) {
						g_clients[id].m_mvl.unlock();
						//pos_lock.unlock();
						SendPacket(id, &pos_p);
					}
					else {
						g_clients[id].m_viewlist.insert(i);
						g_clients[id].m_mvl.unlock();
						//pos_lock.unlock();
						SendPutObjectPacket(id, i);
					}
				}
			}
			// 새로 만난 플레이어에게 시야 추가
			for (auto id : new_vl) {
				if (0 == old_vl.count(id)) {
					g_clients[id].m_mvl.lock();
					if (0 == g_clients[id].m_viewlist.count(i)) {
						g_clients[id].m_viewlist.insert(i);
						g_clients[id].m_mvl.unlock();
						//pos_lock.unlock();
						SendPutObjectPacket(id, i);
					}
					else {
						g_clients[id].m_mvl.unlock();
						//pos_lock.unlock();
						SendPacket(id, &pos_p);
					}
				}
			}
		}
	}
}

int main()
{
	vector <thread> w_threads;
	initialize();
	//CreateWorkerThreads();	// 쓰레드 조인까지 이 안에서 해주어야 한다. 전역변수 해서 관리를 해야 함. 전역변수 만드는 것은
							// 좋은 방법이 아님.
	for (int i = 0; i < 4; ++i) w_threads.push_back(thread{ worker_thread }); // 4인 이유는 쿼드코어 CPU 라서
	//CreateAcceptThreads();
	thread a_thread{ accept_thread };
	thread t_thread{ timer_thread };
//	thread ai_thread{ NPC_ai_thread };
	for (auto& th : w_threads) th.join();
	t_thread.join();
	a_thread.join();
//	ai_thread.join();
}