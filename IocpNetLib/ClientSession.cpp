#include <WinSock2.h>
#include <Mswsock.h>

#include "ContentsConfig.h"
#include "Exception.h"
#include "OverlappedIOContext.h"
#include "ClientSession.h"
#include "IocpManager.h"
#include "ClientSessionManager.h"
#include "Player.h"

const int CLIENT_BUFSIZE = 65536;


ClientSession::ClientSession() : Session(CLIENT_BUFSIZE, CLIENT_BUFSIZE), mPlayer(this)
{
	memset(&mClientAddr, 0, sizeof(SOCKADDR_IN));
}

ClientSession::~ClientSession()
{
}

void ClientSession::SessionReset()
{
	mConnected = 0;
	mRefCount = 0;
	memset(&mClientAddr, 0, sizeof(SOCKADDR_IN));

	mRecvBuffer.BufferReset();

	mSendBufferLock.EnterLock();
	mSendBuffer.BufferReset();
	mSendBufferLock.LeaveLock();

	LINGER lingerOption;
	lingerOption.l_onoff = 1;
	lingerOption.l_linger = 0;

	/// no TCP TIME_WAIT
	if (SOCKET_ERROR == setsockopt(mSocket, SOL_SOCKET, SO_LINGER, (char*)&lingerOption, sizeof(LINGER)))
	{
		printf_s("[DEBUG] setsockopt linger option error: %d\n", GetLastError());
	}
	closesocket(mSocket);

	mSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	mPlayer.PlayerReset();
}

bool ClientSession::PostAccept()
{
	OverlappedAcceptContext* acceptContext = new OverlappedAcceptContext(this);
	DWORD bytes = 0;
	DWORD flags = 0;
	acceptContext->mWsaBuf.len = 0;
	acceptContext->mWsaBuf.buf = nullptr;

	if (FALSE == AcceptEx(*GIocpManager->GetListenSocket(), mSocket, GIocpManager->mAcceptBuf, 0,
		sizeof(SOCKADDR_IN)+16, sizeof(SOCKADDR_IN)+16, &bytes, (LPOVERLAPPED)acceptContext))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			DeleteIoContext(acceptContext);
			printf_s("AcceptEx Error : %d\n", GetLastError());

			return false;
		}
	}

	return true;
}

void ClientSession::AcceptCompletion()
{
	if (1 == InterlockedExchange(&mConnected, 1))
	{
		/// already exists?
		CRASH_ASSERT(false);
		return;
	}

	bool resultOk = true;
	do 
	{
		if (SOCKET_ERROR == setsockopt(mSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)GIocpManager->GetListenSocket(), sizeof(SOCKET)))
		{
			printf_s("[DEBUG] SO_UPDATE_ACCEPT_CONTEXT error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

		int opt = 1;
		if (SOCKET_ERROR == setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(int)))
		{
			printf_s("[DEBUG] TCP_NODELAY error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

		opt = 0;
		if (SOCKET_ERROR == setsockopt(mSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&opt, sizeof(int)))
		{
			printf_s("[DEBUG] SO_RCVBUF change error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

		int addrlen = sizeof(SOCKADDR_IN);
		if (SOCKET_ERROR == getpeername(mSocket, (SOCKADDR*)&mClientAddr, &addrlen))
		{
			printf_s("[DEBUG] getpeername error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

		HANDLE handle = CreateIoCompletionPort((HANDLE)mSocket, GIocpManager->GetComletionPort(), (ULONG_PTR)this, 0);
		if (handle != GIocpManager->GetComletionPort())
		{
			printf_s("[DEBUG] CreateIoCompletionPort error: %d\n", GetLastError());
			resultOk = false;
			break;
		}

	} while (false);


	if (!resultOk)
	{
		DisconnectRequest(DR_ONCONNECT_ERROR);
		return;
	}

	printf_s("[DEBUG] Client Connected: IP=%s, PORT=%d\n", inet_ntoa(mClientAddr.sin_addr), ntohs(mClientAddr.sin_port));

	if (false == PostRecv())
	{
		printf_s("[DEBUG] PreRecv error: %d\n", GetLastError());
	}


	//TEST: 요놈의 위치는 원래 C_LOGIN 핸들링 할 때 해야하는거지만 지금은 접속 완료 시점에서 테스트 ㄱㄱ

	//todo: 플레이어 id는 여러분의 플레이어 테이블 상황에 맞게 적절히 고쳐서 로딩하도록 
	static int id = 101;
	++id;
 	//mPlayer.RequestLoad(id);
}

void ClientSession::OnReceive(size_t len)
{
	/// 패킷 파싱하고 처리
	/*protobuf::io::ArrayInputStream arrayInputStream(mRecvBuffer.GetBufferStart(), mRecvBuffer.GetContiguiousBytes());
	protobuf::io::CodedInputStream codedInputStream(&arrayInputStream);

	PacketHeader packetheader;

	while (codedInputStream.ReadRaw(&packetheader, HEADER_SIZE))
	{
	const void* payloadPos = nullptr;
	int payloadSize = 0;

	codedInputStream.GetDirectBufferPointer(&payloadPos, &payloadSize);

	if ( payloadSize < packetheader.mSize ) ///< 패킷 본체 사이즈 체크
	break;

	if (packetheader.mType >= MAX_PKT_TYPE || packetheader.mType <= 0)
	{
	DisconnectRequest(DR_ACTIVE);
	break;;
	}

	/// payload 읽기
	protobuf::io::ArrayInputStream payloadArrayStream(payloadPos, packetheader.mSize);
	protobuf::io::CodedInputStream payloadInputStream(&payloadArrayStream);

	/// packet dispatch...
	HandlerTable[packetheader.mType](this, packetheader, payloadInputStream);

	/// 읽은 만큼 전진 및 버퍼에서 제거
	codedInputStream.Skip(packetheader.mSize); ///< readraw에서 헤더 크기만큼 미리 전진했기때문
	mRecvBuffer.Remove(HEADER_SIZE + packetheader.mSize);

	}*/
}


void ClientSession::OnDisconnect(DisconnectReason dr)
{
	printf_s("[DEBUG] Client Disconnected: Reason=%d IP=%s, PORT=%d \n", dr, inet_ntoa(mClientAddr.sin_addr), ntohs(mClientAddr.sin_port));
}

void ClientSession::OnRelease()
{
	GClientSessionManager->ReturnClientSession(this);
}

