#include <iostream>

#include "Exception.h"
#include "ServerSession.h"
#include "ClientSession.h"
#include "ClientSessionManager.h"
#include "IocpManager.h"


int main()
{
	/// for dump on crash
	SetUnhandledExceptionFilter(ExceptionFilter);

	/// Global Managers
	GClientSessionManager = new ClientSessionManager;
	GIocpManager = new IocpManager;
	

	if (false == GIocpManager->Initialize()) {
		std::cout << "Fail GIocpManager->Initialize" << std::endl;
		return -1;
	}
		
	if (false == GIocpManager->StartIoThreads()) {
		std::cout << "Fail GIocpManager->StartIoThreads" << std::endl;
		return -1;
	}

	std::cout << "Start Server" << std::endl;
	
	/*ServerSession* testServerSession = new ServerSession(CONNECT_SERVER_ADDR, CONNECT_SERVER_PORT);
	if (false == testServerSession->ConnectRequest())
	{
		printf_s("Connect Server [%s] Error \n", CONNECT_SERVER_ADDR);
	}*/


	GIocpManager->StartAccept(); ///< block here...


	GIocpManager->Finalize();

	std::cout << "End Server" << std::endl;

	//delete testServerSession;
	delete GIocpManager;
	delete GClientSessionManager;

	return 0;
}