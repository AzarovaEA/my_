#include <WS2tcpip.h>
#include <WinSock2.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_CONNECTIONS 4
#define CMD_BUF_SIZE 512
#define DATA_BUF_SIZE 1536
#define LOGIN_MSG "Hello Telnet User!\r\nLogin with password:"

typedef struct
{
	char ipStatus;
	char bind[16];
	int bindPort;
} IPSettings;

typedef struct
{
	char commandBuffer[CMD_BUF_SIZE];
	char dataBuffer[DATA_BUF_SIZE];
	char MSTelnet;
	char catchBuffer[CMD_BUF_SIZE];
} Connection;

Connection connections[MAX_CONNECTIONS];
IPSettings ipSettings;
char password[64] = "M\t\n\t\r\n\tM";
int localSockets[MAX_CONNECTIONS];
char loginStatus[MAX_CONNECTIONS];
char loginAttempts[MAX_CONNECTIONS];
int serverSocket;
char kevent[256] = "";

void logMessage(char* msg1, char* msg2, char* msg3)
{
	char timeBuffer[256];
	time_t currentTime;
	struct tm* timeInfo;
	time(&currentTime);
	timeInfo = localtime(&currentTime);
	strncpy(timeBuffer, asctime(timeInfo), 256);
	char* newline = NULL;
	strtok_s(timeBuffer, "\n", &newline);
	printf("TELNET_SERVER: [time]:[%s], %s%s%s", timeBuffer, msg1, msg2, msg3);
}

DWORD WINAPI executeCommand(LPVOID lpParameter)
{
	int clientSocket = (int)lpParameter;
	char outputBuffer[4096] = { 0 };
	size_t outputSize = 0;

	FILE* pipe = _popen(connections[clientSocket].dataBuffer, "r");
	if (pipe)
	{
		while (fgets(connections[clientSocket].commandBuffer, sizeof(connections[clientSocket].commandBuffer), pipe))
		{
			size_t chunkSize = strlen(connections[clientSocket].commandBuffer);
			if (outputSize + chunkSize + 1 <= sizeof(outputBuffer))
			{
				strcat(outputBuffer, connections[clientSocket].commandBuffer);
				outputSize += chunkSize;
			}
			else
			{
				send(localSockets[clientSocket], outputBuffer, outputSize, 0);
				outputSize = 0;
				memset(outputBuffer, 0, sizeof(outputBuffer));
				strcat(outputBuffer, connections[clientSocket].commandBuffer);
				outputSize += chunkSize;
			}
		}
		_pclose(pipe);
	}

	if (outputSize > 0)
	{
		send(localSockets[clientSocket], outputBuffer, outputSize, 0);
	}

	if (connections[clientSocket].MSTelnet == 1)
	{
		char buf[] = "\r\n";
		send(localSockets[clientSocket], buf, sizeof(buf), 0);
	}
	send(localSockets[clientSocket], kevent, strlen(kevent), 0);

	return 0;
}

int handleClientData(char idx)
{
	int n = recv(localSockets[idx], connections[idx].dataBuffer, sizeof(connections[idx].dataBuffer), 0);
	if (n < 0)
	{
		if (GetLastError() == WSAEWOULDBLOCK || GetLastError() == WSAEINPROGRESS)
		{
			return -1;
		}
		closesocket(localSockets[idx]);
		localSockets[idx] = -1;
		loginStatus[idx] = 0;
		logMessage("USER RESET CONNECTION", " ", "\n");
		return -1;
	}

	if (n == 0)
	{
		closesocket(localSockets[idx]);
		localSockets[idx] = -1;
		loginStatus[idx] = 0;
		logMessage("USER CLOSED CONNECTION", " ", "\n");
		return -1;
	}
	connections[idx].dataBuffer[n] = 0;

	if (connections[idx].MSTelnet == -1)
	{
		if (n >= 2 && connections[idx].dataBuffer[0] == 0xFF && connections[idx].dataBuffer[1] == 0xFD)
		{
			connections[idx].MSTelnet = 1;
			char response[] = { 0xFF, 0xFB, 0x01, 0xFF, 0xFB, 0x03, 0xFF, 0xFD, 0x03 };
			send(localSockets[idx], response, sizeof(response), 0);
			return 0;
		}
		else
		{
			connections[idx].MSTelnet = 0;
		}
	}

	char* null = NULL;
	strtok_s(connections[idx].dataBuffer, "\n", &null);
	strtok_s(connections[idx].dataBuffer, "\r", &null);

	if (loginStatus[idx])
	{
		if (strcmp(connections[idx].dataBuffer, "exit") == 0)
		{
			closesocket(localSockets[idx]);
			localSockets[idx] = -1;
			loginStatus[idx] = 0;
			logMessage("user exit ", " ", "\n");
			return 0;
		}

		HANDLE threadHandle = CreateThread(NULL, 0, executeCommand, (LPVOID)idx, 0, NULL);
		if (threadHandle)
		{
			CloseHandle(threadHandle);
		}
	}
	else
	{
		if (strcmp(connections[idx].dataBuffer, password) == 0)
		{
			char loginMsg[] = "LOGIN DONE!\r\n";
			logMessage("info", ":", loginMsg);
			send(localSockets[idx], loginMsg, sizeof(loginMsg), 0);
			loginStatus[idx] = 1;
			send(localSockets[idx], kevent, strlen(kevent), 0);
		}
		else
		{
			logMessage("invalid password of \"", connections[idx].dataBuffer, "\"\n");

			char loginFailedMsg[] = "LOGIN FAILED!\r\nTry again:";
			send(localSockets[idx], loginFailedMsg, sizeof(loginFailedMsg), 0);
			loginAttempts[idx]++;
			if (loginAttempts[idx] > 4)
			{
				closesocket(localSockets[idx]);
				localSockets[idx] = -1;
				loginStatus[idx] = 0;
				logMessage("LOGIN WRONG TOO MUCH", ",", "CLOSED\n");
			}
		}
	}

	return 0;
}

int acceptClient()
{
	struct sockaddr_in clientAddr;
	int addrLen = sizeof(clientAddr);
	int availableSocket = MAX_CONNECTIONS;

	for (int i = 0; i < MAX_CONNECTIONS; i++)
	{
		if (localSockets[i] < 0)
		{
			availableSocket = i;
		}
	}

	localSockets[availableSocket] = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
	if (localSockets[availableSocket] < 0)
	{
		logMessage("ACCEPT CONNECTION ", "err", " \n");
		return -1;
	}

	char ipAddr[16];
	inet_ntop(AF_INET, &(clientAddr.sin_addr), ipAddr, 16);
	logMessage("CONNECTION FROM ", ipAddr, " \n");

	if (availableSocket == MAX_CONNECTIONS)
	{
		char buf[] = "OVERCONNECTION\n";
		logMessage("info", ":", buf);
		int io = 1;
		ioctlsocket(localSockets[MAX_CONNECTIONS], FIONBIO, &io);
		send(localSockets[MAX_CONNECTIONS], buf, sizeof(buf), 0);
		closesocket(localSockets[MAX_CONNECTIONS]);
		localSockets[MAX_CONNECTIONS] = -1;
		return -1;
	}

	int io = 1;
	ioctlsocket(localSockets[availableSocket], FIONBIO, &io);
	loginAttempts[availableSocket] = 0;
	connections[availableSocket].MSTelnet = -1;
	memset(connections[availableSocket].catchBuffer, 0, CMD_BUF_SIZE);

	send(localSockets[availableSocket], LOGIN_MSG, strlen(LOGIN_MSG), 0);
	return 0;
}

void showHelp();

int main()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0)
	{
		printf("Init WSAStartup error.\n");
		return -1;
	}

	char input[256];
	printf("Enter command line arguments: ");
	fgets(input, sizeof(input), stdin);

	char* argv[10];
	int argc = 0;
	char* nextToken = NULL;
	char* token = strtok_s(input, " \n", &nextToken);
	while (token != NULL && argc < 10)
	{
		argv[argc++] = token;
		token = strtok_s(NULL, " \n", &nextToken);
	}

	if (argc <= 1)
	{
		showHelp();
	}

	ipSettings.bindPort = 23;
	ipSettings.ipStatus = 4;
	strncpy(ipSettings.bind, "0.0.0.0", 10);

	for (int i = 0; i < argc; i += 2)
	{
		if (i + 1 >= argc)
		{
			showHelp();
			return -1;
		}

		if (strcmp(argv[i], "-ip") == 0)
		{
			if (strlen(argv[i + 1]) < 16)
			{
				strncpy(ipSettings.bind, argv[i + 1], 16);
			}
		}
		else if (strcmp(argv[i], "-p") == 0)
		{
			if (strlen(argv[i + 1]) < 7)
			{
				ipSettings.bindPort = atoi(argv[i + 1]);
			}
		}
		else if (strcmp(argv[i], "-key") == 0)
		{
			if (strlen(argv[i + 1]) < 64)
			{
				strcpy_s(password, 64, argv[i + 1]);
			}
		}
		else
		{
			showHelp();
			return -1;
		}
	}

	printf("Telnet is now ONLINE on %s:%d, using ipv%d\n", ipSettings.bind, ipSettings.bindPort, ipSettings.ipStatus);

	char* buf = NULL;
	size_t len = 0;
	_dupenv_s(&buf, &len, "USERPROFILE");
	snprintf(kevent, 256, "%s>", buf);
	struct sockaddr_in serverAddr;

	for (int i = 0; i < MAX_CONNECTIONS; i++)
	{
		localSockets[i] = -1;
	}

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket < 0)
	{
		printf("Server socket err!\n");
		exit(-1);
	}

	int io = 1;
	setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&io, sizeof(io));

	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	inet_pton(AF_INET, ipSettings.bind, &(serverAddr.sin_addr));
	serverAddr.sin_port = htons(ipSettings.bindPort);
	if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
	{
		printf("Error: Couldn't bind to address %s port %d\n", ipSettings.bind, ipSettings.bindPort);
		closesocket(serverSocket);
		exit(-1);
	}

	if (listen(serverSocket, 5) < 0)
	{
		printf("Error: Couldn't listen on address %s port %d\n", ipSettings.bind, ipSettings.bindPort);
		closesocket(serverSocket);
		exit(-1);
	}

	io = 1;
	ioctlsocket(serverSocket, FIONBIO, &io);

	while (1)
	{
		int maxfd = 0;
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(serverSocket, &readfds);

		for (int i = 0; i < MAX_CONNECTIONS; i++)
		{
			if (localSockets[i] != -1)
			{
				FD_SET(localSockets[i], &readfds);
			}
			if (localSockets[i] > maxfd)
			{
				maxfd = localSockets[i];
			}
		}

		if (select(maxfd + 1, &readfds, NULL, NULL, NULL) == 0)
		{
			printf("SLEEPING\n");
			Sleep(16);
			continue;
		}

		if (FD_ISSET(serverSocket, &readfds))
		{
			acceptClient();
		}

		for (int i = 0; i < MAX_CONNECTIONS; i++)
		{
			if (localSockets[i] != -1 && FD_ISSET(localSockets[i], &readfds))
			{
				handleClientData(i);
			}
		}
	}
	return 0;
}

void showHelp()
{
	printf("Usage: set all the variables\n");
	printf("  -ip, --listen address\n");
	printf("  -p, --port\n");
	printf("  -key, --key, set password\n");
	printf("  -h, --help\n");
	printf("Examples:\n");
	printf("-ip 127.0.0.1 -p 23 -key qwerty\n");
	getchar();
	main();
}
