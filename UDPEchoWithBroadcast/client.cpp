//
// Bachelor of Software Engineering
// Media Design School
// Auckland
// New Zealand
//
// (c) 2015 Media Design School
//
// File Name	: 
// Description	: 
// Author		: Your Name
// Mail			: your.name@mediadesign.school.nz
//

//Library Includes
#include <WS2tcpip.h>
#include <Windows.h>
#include <iostream>
#include <thread>
#include <string>

//Local Includes
#include "utils.h"
#include "consoletools.h"
#include "network.h"
#include "networkentity.h"
#include "socket.h"

//This includes
#include "client.h"

CClient::CClient()
	:m_pcPacketData(0)
	, m_pClientSocket(0)
{
	ZeroMemory(&m_ServerSocketAddress, sizeof(m_ServerSocketAddress));

	//Create a Packet Array and fill it out with all zeros.
	m_pcPacketData = new char[MAX_MESSAGE_LENGTH];
	ZeroMemory(m_pcPacketData, MAX_MESSAGE_LENGTH);

}

CClient::~CClient()
{
	
	delete[] m_pcPacketData;
	m_pcPacketData = 0;

	delete m_pClientSocket;
	m_pClientSocket = 0;

	delete m_pWorkQueue;
	m_pWorkQueue = 0;
	
}

/***********************
* Initialise: Initialises a client object by creating a client socket and filling out the socket address structure with details of server to send the data to.
* @author: 
* @parameter: none
* @return: void
********************/
bool CClient::Initialise()
{
	//Local Variables to hold Server's IP address and Port NUmber as entered by the user
	char _cServerIPAddress[128];
	ZeroMemory(&_cServerIPAddress, 128);
	char _cServerPort[10];
	ZeroMemory(&_cServerPort, 10);
	unsigned short _usServerPort;

	//Local variable to hold the index of the server chosen to connect to
	char _cServerChosen[5];
	ZeroMemory(_cServerChosen, 5);
	unsigned int _uiServerIndex;

	//Local variable to hold client's name
	char _cUserName[50];
	ZeroMemory(&m_cUserName, 50);

	//Zero out the memory for all the member variables.
	ZeroMemory(&m_cUserName, strlen(m_cUserName));

	//Create a work queue to distribute messages between the main  thread and the receive thread.
	m_pWorkQueue = new CWorkQueue<std::string>();

	//Create a socket object
	m_pClientSocket = new CSocket();
	
	//Get the port number to bind the socket to
	unsigned short _usClientPort = QueryPortNumber(DEFAULT_CLIENT_PORT);
	//Initialise the socket to the port number
	if (!m_pClientSocket->Initialise(_usClientPort))
	{
		return false;
	}

	//Set the client's online status to true
	m_bOnline = true;

	//Use a boolean flag to determine if a valid server has been chosen by the client or not
	bool _bServerChosen = false;

	do {
#pragma region _GETSERVER_
		unsigned char _ucChoice = QueryOption("Do you want to broadcast for servers or manually connect (B/M)?", "BM");

		switch (_ucChoice)
		{
		case 'B':
		{
			//Question 7: Broadcast to detect server
			m_bDoBroadcast = true;
			m_pClientSocket->EnableBroadcast();
			BroadcastForServers();
			if (m_vecServerAddr.size() == 0)
			{
				std::cout << "No Servers Found " << std::endl;
				continue;
			}
			else {

				//Give a list of servers for the user to choose from :
				for (unsigned int i = 0; i < m_vecServerAddr.size(); i++)
				{
					std::cout << std::endl << "[" << i << "]" << " SERVER : found at " << ToString(m_vecServerAddr[i]) << std::endl;
				}
				std::cout << "Choose a server number to connect to :";
				gets_s(_cServerChosen);

				_uiServerIndex = atoi(_cServerChosen);
				m_ServerSocketAddress.sin_family = AF_INET;
				m_ServerSocketAddress.sin_port = m_vecServerAddr[_uiServerIndex].sin_port;
				m_ServerSocketAddress.sin_addr.S_un.S_addr = m_vecServerAddr[_uiServerIndex].sin_addr.S_un.S_addr;
				std::string _strServerAddress = ToString(m_vecServerAddr[_uiServerIndex]);
				std::cout << "Attempting to connect to server at " << _strServerAddress << std::endl;
				_bServerChosen = true;
			}
			m_bDoBroadcast = false;
			m_pClientSocket->DisableBroadcast();
			break;
		}
		case 'M':
		{
			std::cout << "Enter server IP or empty for localhost: ";

			gets_s(_cServerIPAddress);
			if (_cServerIPAddress[0] == 0)
			{
				strcpy_s(_cServerIPAddress, "127.0.0.1");
			}
			//Get the Port Number of the server
			std::cout << "Enter server's port number or empty for default server port: ";
			gets_s(_cServerPort);
			//std::cin >> _usServerPort;

			if (_cServerPort[0] == 0)
			{
				_usServerPort = DEFAULT_SERVER_PORT;
			}
			else
			{
				_usServerPort = atoi(_cServerPort);
			}
			//Fill in the details of the server's socket address structure.
			//This will be used when stamping address on outgoing packets
			m_ServerSocketAddress.sin_family = AF_INET;
			m_ServerSocketAddress.sin_port = htons(_usServerPort);
			inet_pton(AF_INET, _cServerIPAddress, &m_ServerSocketAddress.sin_addr);
			_bServerChosen = true;
			std::cout << "Attempting to connect to server at " << _cServerIPAddress << ":" << _usServerPort << std::endl;
			break;
		}
		default:
		{
			std::cout << "This is not a valid option" << std::endl;
			return false;
			break;
		}
		}
#pragma endregion _GETSERVER_

	} while (_bServerChosen == false);

	//Send a hanshake message to the server as part of the Client's Initialization process.
	TPacket _packet;
	m_clientState = CLIENT_NO_STATE;

	while (m_clientState == CLIENT_NO_STATE || m_clientState == CLIENT_CONNECT_FAIL)
	{
		std::cout << "Please enter a username : ";
		gets_s(_cUserName);
		_packet.Serialize(HANDSHAKE, _cUserName); 
		SendData(_packet.PacketData);

		WaitForHandshake(); //Function will change value of m_clientState

		if (m_clientState == CLIENT_CONNECT_FAIL)
		{
			std::cout << std::endl << "Error: Username is taken. Please choose a new username." << std::endl;
		}
		else if (m_clientState == CLIENT_TIMEOUT)
		{
			std::cout << std::endl << "Error: Server not responding. Exiting program." << std::endl;
			CNetwork::GetInstance().ShutDown();
			m_bOnline = false;
			return false;
		}
	}

	//Success! connected to server
	strcpy_s(m_cUserName, 50, _cUserName); //Store username

	_packet.Serialize(SERVER_LIST, "");
	SendData(_packet.PacketData);


	return true;
}

bool CClient::WaitForHandshake()
{
	char bufferToRecieveData[MAX_MESSAGE_LENGTH];

	//set a timer on the socket for one second
	struct timeval timeValue;
	timeValue.tv_sec = 1;
	timeValue.tv_usec = 0;
	setsockopt(m_pClientSocket->GetSocketHandle(), SOL_SOCKET, SO_RCVTIMEO,
		(char*)&timeValue, sizeof(timeValue));

	//Receive data into a local buffer
	char _buffer[MAX_MESSAGE_LENGTH];
	sockaddr_in _FromAddress;
	int iSizeOfAdd = sizeof(sockaddr_in);
	//char _pcAddress[50];

	m_clientState = CLIENT_TIMEOUT; //If state does not change, time out

	while (true)
	{
		// pull off the packet(s) using recvfrom()
		int _iNumOfBytesReceived = recvfrom(				// pulls a packet from a single source...
			this->m_pClientSocket->GetSocketHandle(),	// client-end socket being used to read from
			_buffer,									// incoming packet to be filled
			MAX_MESSAGE_LENGTH,							// length of incoming packet to be filled
			0,											// flags
			reinterpret_cast<sockaddr*>(&_FromAddress),	// address to be filled with packet source
			&iSizeOfAdd								// size of the above address struct.
		);

		if (_iNumOfBytesReceived < 0)
		{
			//Error in receiving data 
			int _iError = WSAGetLastError();
			//std::cout << "recvfrom failed with error " << _iError;
			if (_iError == WSAETIMEDOUT) // Socket timed out on Receive
			{
				m_bDoBroadcast = false; //Do not broadcast any more
				break;
			}
		}
		else if (_iNumOfBytesReceived == 0)
		{
			//The remote end has shutdown the connection
		}
		else
		{
			//There is valid data received.
			strcpy_s(bufferToRecieveData, strlen(_buffer) + 1, _buffer);
			TPacket _packet;
			_packet.Deserialize(bufferToRecieveData);
			 
			if ((std::string)_packet.MessageContent == "accept")
			{
				m_clientState = CLIENT_CONNECT_PASS;
				return true;
			}
			else if ((std::string)_packet.MessageContent == "fail")
			{
				m_clientState = CLIENT_CONNECT_FAIL;
				return false;
			}
			else
			{
				std::cout << "Unknown handshake message recieved: " << _packet.MessageContent;
			}
			break; //Got handshake reply
		}
	}//End of while loop

	return false;
}

void CClient::ProcessClientInput(EMessageType messageType, char* message)
{
	TPacket _packet;

	//Check message of string is commmand
	if (message[0] == '\0')
	{
		return; //Client didn't type anything
	}

	if (message[0] == '!') //Command inputted!
	{
		if (message[1] != '!')
		{

			std::string command(++message); //Ignore initial '!'
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 10);

			if (command == "?")
			{
				std::cout << s_CommandList << std::endl;
			}
			else if (command == "q")
			{
				_packet.Serialize(QUIT, "");
				SendData(_packet.PacketData);
				m_bOnline = false;
				CNetwork::GetInstance().ShutDown(); //Close client
			}
			else if (command == "a")
			{
				_packet.Serialize(SERVER_LIST, "");
				SendData(_packet.PacketData);
			}
			else
			{
				std::cout << "<< Invalid command. Type !? for command list >>" << std::endl;
			}

			return;
		}
		else //User just wanted to type '!'
		{
			message = "!";
		}
	}

	//Normal message, send to server
	_packet.Serialize(messageType, message);
	SendData(_packet.PacketData);
}

bool CClient::ReconnectToServer()
{
	TPacket _packet;
	m_clientState = CLIENT_NO_STATE;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 10);

	auto startTime = std::chrono::system_clock::now();
	double handShakeWaitPeriod = 5.0;

	_packet.Serialize(HANDSHAKE, m_cUserName);
	SendData(_packet.PacketData);

	while (m_clientState == CLIENT_NO_STATE || m_clientState == CLIENT_CONNECT_FAIL)
	{
		//Keep processing data while waiting
		if (!m_pWorkQueue->empty())
		{
			std::string temp;
			m_pWorkQueue->pop(temp);
			ProcessData(const_cast<char*>(temp.c_str()));
		}

		auto endTime = std::chrono::system_clock::now();
		std::chrono::duration<double> elapsedTime = endTime - startTime;

		if (elapsedTime.count() >= handShakeWaitPeriod)
		{
			m_clientState = CLIENT_TIMEOUT;
		}

		
		//Wait for state to change (getting packets from ProcessData)
		if (m_clientState == CLIENT_CONNECT_FAIL)
		{
			std::cout << std::endl << "Error: Username is now taken. Please enter new username." << std::endl;
			gets_s(m_cUserName);
			_packet.Serialize(HANDSHAKE, m_cUserName);
			SendData(_packet.PacketData);
		}
		else if (m_clientState == CLIENT_TIMEOUT)
		{
			std::cout << std::endl << "Error: Server not responding. Exiting program...Press anything to continue..." << std::endl;
			CNetwork::GetInstance().ShutDown();
			m_bOnline = false;
			_getch();
			return false;
		}
	}
	//Successfully reconnect
	return true;
}

bool CClient::BroadcastForServers()
{
	//Make a broadcast packet
	TPacket _packet;
	_packet.Serialize(BROADCAST, "Broadcast to Detect Server");

	char _pcTempBuffer[MAX_MESSAGE_LENGTH];
	//Send out a broadcast message using the broadcast address
	m_pClientSocket->SetRemoteAddress(INADDR_BROADCAST);
	m_pClientSocket->SetRemotePort(DEFAULT_SERVER_PORT);

	m_ServerSocketAddress.sin_family = AF_INET;
	m_ServerSocketAddress.sin_addr.S_un.S_addr = INADDR_BROADCAST;

	for (int i = 0; i < 10; i++) //Just try  a series of 10 ports to detect a runmning server; this is needed since we are testing multiple servers on the same local machine
	{
		m_ServerSocketAddress.sin_port = htons(DEFAULT_SERVER_PORT + i);
		SendData(_packet.PacketData);
	}
	ReceiveBroadcastMessages(_pcTempBuffer);

	return true;

}

void CClient::ReceiveBroadcastMessages(char* _pcBufferToReceiveData)
{
	//set a timer on the socket for one second
	struct timeval timeValue;
	timeValue.tv_sec = 1;
	timeValue.tv_usec = 0;
	setsockopt(m_pClientSocket->GetSocketHandle(), SOL_SOCKET, SO_RCVTIMEO,
		(char*)&timeValue, sizeof(timeValue));

	//Receive data into a local buffer
	char _buffer[MAX_MESSAGE_LENGTH];
	sockaddr_in _FromAddress;
	int iSizeOfAdd = sizeof(sockaddr_in);
	//char _pcAddress[50];

	while (m_bDoBroadcast)
	{
		// pull off the packet(s) using recvfrom()
		int _iNumOfBytesReceived = recvfrom(				// pulls a packet from a single source...
			this->m_pClientSocket->GetSocketHandle(),	// client-end socket being used to read from
			_buffer,									// incoming packet to be filled
			MAX_MESSAGE_LENGTH,							// length of incoming packet to be filled
			0,											// flags
			reinterpret_cast<sockaddr*>(&_FromAddress),	// address to be filled with packet source
			&iSizeOfAdd								// size of the above address struct.
		);

		if (_iNumOfBytesReceived < 0)
		{
			//Error in receiving data 
			int _iError = WSAGetLastError();
			//std::cout << "recvfrom failed with error " << _iError;
			if (_iError == WSAETIMEDOUT) // Socket timed out on Receive
			{
				m_bDoBroadcast = false; //Do not broadcast any more
				break;
			}
			_pcBufferToReceiveData = 0;
		}
		else if (_iNumOfBytesReceived == 0)
		{
			//The remote end has shutdown the connection
			_pcBufferToReceiveData = 0;
		}
		else
		{
			//There is valid data received.
			strcpy_s(_pcBufferToReceiveData, strlen(_buffer) + 1, _buffer);
			m_ServerSocketAddress = _FromAddress;
			m_vecServerAddr.push_back(m_ServerSocketAddress);
		}
	}//End of while loop
}

bool CClient::SendData(char* _pcDataToSend)
{
	int _iBytesToSend = (int)strlen(_pcDataToSend) + 1;
	
	char _RemoteIP[MAX_ADDRESS_LENGTH];
	inet_ntop(AF_INET, &m_ServerSocketAddress.sin_addr, _RemoteIP, sizeof(_RemoteIP));
	//std::cout << "Trying to send " << _pcDataToSend << " to " << _RemoteIP << ":" << ntohs(m_ServerSocketAddress.sin_port) << std::endl;
	char _message[MAX_MESSAGE_LENGTH];
	strcpy_s(_message, strlen(_pcDataToSend) + 1, _pcDataToSend);

	int iNumBytes = sendto(
		m_pClientSocket->GetSocketHandle(),				// socket to send through.
		_pcDataToSend,									// data to send
		_iBytesToSend,									// number of bytes to send
		0,												// flags
		reinterpret_cast<sockaddr*>(&m_ServerSocketAddress),	// address to be filled with packet target
		sizeof(m_ServerSocketAddress)							// size of the above address struct.
		);
	//iNumBytes;
	if (_iBytesToSend != iNumBytes)
	{
		std::cout << "There was an error in sending data from client to server" << std::endl;
		return false;
	}
	return true;
}

void CClient::ReceiveData(char* _pcBufferToReceiveData)
{
	sockaddr_in _FromAddress; // Make a local variable to extract the IP and port number of the sender from whom we are receiving
	//In this case; it should be the details of the server; since the client only ever receives from the server
	int iSizeOfAdd = sizeof(_FromAddress);
	int _iNumOfBytesReceived;
	
	//Receive data into a local buffer
	char _buffer[MAX_MESSAGE_LENGTH];
	//For debugging purpose only, convert the Address structure to a string.
	char _pcAddress[50];
	ZeroMemory(&_pcAddress, 50);
	while(m_bOnline)
	{
		// pull off the packet(s) using recvfrom()
		_iNumOfBytesReceived = recvfrom(			// pulls a packet from a single source...
			this->m_pClientSocket->GetSocketHandle(),						// client-end socket being used to read from
			_buffer,							// incoming packet to be filled
			MAX_MESSAGE_LENGTH,					   // length of incoming packet to be filled
			0,										// flags
			reinterpret_cast<sockaddr*>(&_FromAddress),	// address to be filled with packet source
			&iSizeOfAdd								// size of the above address struct.
			);
		inet_ntop(AF_INET, &_FromAddress, _pcAddress, sizeof(_pcAddress));

		if (_iNumOfBytesReceived < 0)
		{
			//Error in receiving data 
			//std::cout << "recvfrom failed with error " << WSAGetLastError();
			_pcBufferToReceiveData = 0;
		}
		else if (_iNumOfBytesReceived == 0)
		{
			//The remote end has shutdown the connection
			_pcBufferToReceiveData = 0;
		}
		else
		{
			//There is valid data received.
			strcpy_s(m_pcPacketData, strlen(_buffer) + 1, _buffer);
			//strcpy_s(m_pcPacketData, strlen(_buffer) + 1, _buffer);
			//Put this packet data in the workQ
			m_ServerSocketAddress = _FromAddress;
			m_pWorkQueue->push(m_pcPacketData);
		}
		//std::this_thread::yield(); //Yield the processor; giving the main a chance to run.
	}
}

void CClient::ProcessData(char* _pcDataReceived)
{
	TPacket _packetRecvd;
	_packetRecvd = _packetRecvd.Deserialize(_pcDataReceived);
	switch (_packetRecvd.MessageType)
	{
	case HANDSHAKE:
	{
		std::unique_lock<std::mutex> sendingLock(m_clientMutex);
		m_clientState = ((std::string)_packetRecvd.MessageContent == "accept") ? CLIENT_CONNECT_PASS : CLIENT_CONNECT_FAIL;
		break;
	}
	case DATA:
	{
		//Get which user typed previous message
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 10);
		std::cout << _packetRecvd.MessageContent << std::endl;
		break;
	}
	case CONNECTION_ERROR:
	{
		//Client knows they are disconnected from server, try reconnect
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 10);
		std::cout << "<< You are not connected to the server. Attempting to reconnect. >>" << std::endl;

		if (ReconnectToServer()) //Function will reconnect to server if possible
		{
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 10);
			std::cout << "   [ Reconnection complete! ]" << std::endl;
			TPacket _packet;
			_packet.Serialize(SERVER_LIST, ""); //Get server list again
			SendData(_packet.PacketData);
		}
		break;
	}
	case KEEPALIVE:
	{
		TPacket _packet;
		_packet.Serialize(KEEPALIVE, _packetRecvd.MessageContent);
		SendData(_packet.PacketData);
		break;
	}
	default:
		break;

	}
}

void CClient::GetRemoteIPAddress(char *_pcSendersIP)
{
	inet_ntop(AF_INET, &(m_ServerSocketAddress.sin_addr), _pcSendersIP, sizeof(_pcSendersIP));
	return;
}

unsigned short CClient::GetRemotePort()
{
	return ntohs(m_ServerSocketAddress.sin_port);
}

void CClient::GetPacketData(char* _pcLocalBuffer)
{
	strcpy_s(_pcLocalBuffer, strlen(m_pcPacketData) + 1, m_pcPacketData);
}

CWorkQueue<std::string>* CClient::GetWorkQueue()
{
	return m_pWorkQueue;
}