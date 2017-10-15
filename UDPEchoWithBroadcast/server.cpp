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
#include <iostream>
#include <utility>
#include <thread>
#include <chrono>

//Local Includes
#include "utils.h"
#include "network.h"
#include "consoletools.h"
#include "socket.h"


//Local Includes
#include "server.h"

CServer::CServer()
	:m_pcPacketData(0),
	m_pServerSocket(0)
{
	ZeroMemory(&m_ClientAddress, sizeof(m_ClientAddress));
}

CServer::~CServer()
{
	
	delete m_pConnectedClients;
	m_pConnectedClients = 0;

	delete m_pServerSocket;
	m_pServerSocket = 0;

	delete m_pWorkQueue;
	m_pWorkQueue = 0;
	
	delete[] m_pcPacketData;
	m_pcPacketData = 0;
	
}

bool CServer::Initialise()
{
	//Set up first time to call keep alive messages
	m_lastKeepAliveTime = std::chrono::system_clock::now();
	m_keepAliveState = WAITING_NEXT_KEEP_ALIVE;

	m_pcPacketData = new char[MAX_MESSAGE_LENGTH];
	
	//Create a work queue to distribute messages between the main  thread and the receive thread.
	m_pWorkQueue = new CWorkQueue<std::string>();

	//Create a socket object
	m_pServerSocket = new CSocket();

	//Get the port number to bind the socket to
	unsigned short _usServerPort = QueryPortNumber(DEFAULT_SERVER_PORT);

	//Initialise the socket to the local loop back address and port number
	if (!m_pServerSocket->Initialise(_usServerPort))
	{
		return false;
	}

	//Qs 2: Create the map to hold details of all connected clients
	m_pConnectedClients = new std::map < std::string, TClientDetails >() ;

	return true;
}

bool CServer::AddClient(std::string _strClientName)
{

	for (auto it = m_pConnectedClients->begin(); it != m_pConnectedClients->end(); ++it)
	{

		//Check to see that the client to be added does not already exist in the map, 
		if(it->first == ToString(m_ClientAddress))
		{
			return false;
		}
		//also check for the existence of the username
		else if (it->second.m_strName == _strClientName)
		{
			return false;
		}
	}

	//Tell client they have been accepted
	TPacket _packet;
	_packet.Serialize(HANDSHAKE, "accept");
	SendData(_packet.PacketData);


	//Add the client to the map.
	TClientDetails _clientToAdd;
	_clientToAdd.m_strName = _strClientName;
	_clientToAdd.m_bIsActive = true;
	_clientToAdd.m_ClientAddress = this->m_ClientAddress;

	std::string _strAddress = ToString(m_ClientAddress);
	m_pConnectedClients->insert(std::pair < std::string, TClientDetails > (_strAddress, _clientToAdd));

	//Tell everyone a new client is coming!
	std::string message = "<< " + _clientToAdd.m_strName + " has joined the server! >>";
	_packet.Serialize(DATA, const_cast<char*>(message.c_str()));
	SendDataToAllClients(_packet.PacketData);

	return true;
}

bool CServer::SendData(char* _pcDataToSend)
{
	int _iBytesToSend = (int)strlen(_pcDataToSend) + 1;
	
	int iNumBytes = sendto(
		m_pServerSocket->GetSocketHandle(),				// socket to send through.
		_pcDataToSend,									// data to send
		_iBytesToSend,									// number of bytes to send
		0,												// flags
		reinterpret_cast<sockaddr*>(&m_ClientAddress),	// address to be filled with packet target
		sizeof(m_ClientAddress)							// size of the above address struct.
		);
	//iNumBytes;
	if (_iBytesToSend != iNumBytes)
	{
		std::cout << "There was an error in sending data from client to server" << std::endl;
		return false;
	}
	return true;
}

void CServer::ReceiveData(char* _pcBufferToReceiveData)
{

	int iSizeOfAdd = sizeof(m_ClientAddress);
	int _iNumOfBytesReceived;
	int _iPacketSize;

	//Make a thread local buffer to receive data into
	char _buffer[MAX_MESSAGE_LENGTH];

	while (true)
	{
		// pull off the packet(s) using recvfrom()
		_iNumOfBytesReceived = recvfrom(			// pulls a packet from a single source...
			m_pServerSocket->GetSocketHandle(),						// client-end socket being used to read from
			_buffer,							// incoming packet to be filled
			MAX_MESSAGE_LENGTH,					   // length of incoming packet to be filled
			0,										// flags
			reinterpret_cast<sockaddr*>(&m_ClientAddress),	// address to be filled with packet source
			&iSizeOfAdd								// size of the above address struct.
		);
		if (_iNumOfBytesReceived < 0)
		{
			int _iError = WSAGetLastError();
			ErrorRoutines::PrintWSAErrorInfo(_iError);
			//return false;
		}
		else
		{
			_iPacketSize = static_cast<int>(strlen(_buffer)) + 1;
			strcpy_s(_pcBufferToReceiveData, _iPacketSize, _buffer);
			char _IPAddress[100];
			inet_ntop(AF_INET, &m_ClientAddress.sin_addr, _IPAddress, sizeof(_IPAddress));
			
			std::cout << "Server Received \"" << _pcBufferToReceiveData << "\" from " <<
				_IPAddress << ":" << ntohs(m_ClientAddress.sin_port) << std::endl;
			//Push this packet data into the WorkQ
			m_pWorkQueue->push(_pcBufferToReceiveData);
		}
		//std::this_thread::yield();
		
	} //End of while (true)
}

void CServer::GetRemoteIPAddress(char *_pcSendersIP)
{
	char _temp[MAX_ADDRESS_LENGTH];
	int _iAddressLength;
	inet_ntop(AF_INET, &(m_ClientAddress.sin_addr), _temp, sizeof(_temp));
	_iAddressLength = static_cast<int>(strlen(_temp)) + 1;
	strcpy_s(_pcSendersIP, _iAddressLength, _temp);
}

unsigned short CServer::GetRemotePort()
{
	return ntohs(m_ClientAddress.sin_port);
}

void CServer::ProcessData(char* _pcDataReceived)
{
	std::unique_lock<std::mutex> sendingLock(m_sendingPacketMutex); //Make sure address is consistent

	TPacket _packetRecvd, _packetToSend;
	_packetRecvd = _packetRecvd.Deserialize(_pcDataReceived);

	std::string stuff = (std::string)_packetRecvd.MessageContent;
	std::cout << "[MESSAGE CONTENT DURING PROCESSING]: " + stuff << std::endl;

	//Check if messagetype not handshake/broadcast + user is connected
	bool clientInMap = (*m_pConnectedClients).find(ToString(m_ClientAddress)) != (*m_pConnectedClients).end();
	bool typeToCheck = _packetRecvd.MessageType != BROADCAST && _packetRecvd.MessageType != HANDSHAKE;

	if (typeToCheck && !clientInMap)
	{
		//Message is invalid
		TPacket _packet;
		_packet.Serialize(CONNECTION_ERROR, ""); //Inform client they are not in map
		SendData(_packet.PacketData);
		return;
	}
	
	switch (_packetRecvd.MessageType)
	{
	case HANDSHAKE:
	{
		std::cout << "Server received a handshake message " << std::endl;
		if (!AddClient(_packetRecvd.MessageContent))
		{
			_packetToSend.Serialize(HANDSHAKE, "fail"); //Inform client, handshake fail
			SendData(_packetToSend.PacketData);
		}
		break;
	}
	case DATA:
	{
		std::string name = (*m_pConnectedClients)[ToString(m_ClientAddress)].m_strName;
		std::string message = "[" + name + "] " + _packetRecvd.MessageContent;

		_packetToSend.Serialize(DATA, const_cast<char*>(message.c_str()));
		SendDataToAllClients(_packetToSend.PacketData);

		break;
	}

	case BROADCAST:
	{
		std::cout << "Received a broadcast packet" << std::endl;
		//Just send out a packet to the back to the client again which will have the server's IP and port in it's sender fields
		_packetToSend.Serialize(BROADCAST, "I'm here!");
		SendData(_packetToSend.PacketData);
		break;
	}
	case SERVER_LIST:
	{
		std::string currentUserString = "<<Online Users>> : ";

		for (auto it = m_pConnectedClients->begin(); it != m_pConnectedClients->end(); ++it)
		{
			currentUserString += "[" + it->second.m_strName + "] ";
		}

		_packetToSend.Serialize(DATA, const_cast<char*>(currentUserString.c_str()));
		SendData(_packetToSend.PacketData);

		break;
	}
	case QUIT:
	{
		auto it = (*m_pConnectedClients).find(ToString(m_ClientAddress));

		if (it != (*m_pConnectedClients).end()) //Find and delete client from client map
		{
			TPacket _packet;
			std::string message = "<< " + it->second.m_strName + " has left the server >>";
			_packet.Serialize(DATA, const_cast<char*>(message.c_str()));
			(*m_pConnectedClients).erase(it);

			SendDataToAllClients(_packet.PacketData); //Tell all other users client has left
		}

	}
	case KEEPALIVE:
	{
		(*m_pConnectedClients)[ToString(_packetRecvd.MessageContent)].m_bIsActive = true;
		break;
	}
	default:
		break;

	}
}

CWorkQueue<std::string>* CServer::GetWorkQueue()
{
	return m_pWorkQueue;
}

void CServer::SendDataToAllClients(char * _pcDataToSend)
{
	for (auto it = m_pConnectedClients->begin(); it != m_pConnectedClients->end(); ++it)
	{
		m_ClientAddress = it->second.m_ClientAddress;
		SendData(_pcDataToSend);
	}
}

void CServer::KeepAliveLogic()
{
	std::chrono::time_point<std::chrono::system_clock> currentTime = std::chrono::system_clock::now();

	std::chrono::duration<double> elapsedTime = currentTime - m_lastKeepAliveTime;

	switch (m_keepAliveState)
	{
	case WAITING_NEXT_KEEP_ALIVE:
	{
		if (elapsedTime.count() > TIME_BETWEEN_KEEP_ALIVE_MESSAGES)
		{
			std::cout << "Sending out keep alive messages" << std::endl;

			//Make sure address is consistent
			std::unique_lock<std::mutex> sendingLock(m_sendingPacketMutex);

			TPacket _packet;

			//Time for keep alive checkup
			for (auto it = m_pConnectedClients->begin(); it != m_pConnectedClients->end(); ++it)
			{
				_packet.Serialize(KEEPALIVE, const_cast<char*>(ToString(it->second.m_ClientAddress).c_str()));
				it->second.m_bIsActive = false;
				m_ClientAddress = it->second.m_ClientAddress;
				SendData(_packet.PacketData);
			}

			m_keepAliveState = WAITING_CLIENT_REPLY;
			m_lastKeepAliveTime = currentTime; //Set for next check up time
		}
		break;
	}
	case WAITING_CLIENT_REPLY:
	{
		if (elapsedTime.count() > TIME_WAIT_FOR_KEEP_ALIVE_MESSAGE)
		{
			std::cout << "Checking replies to get alive messages" << std::endl;

			//Use mutex so editing client map does not affect rest of program
			std::unique_lock<std::mutex> sendingLock(m_sendingPacketMutex);

			std::vector<std::map<std::string, TClientDetails>::iterator> itToDelete;

			//Time to check people who replied. No reply = die
			for (auto it = m_pConnectedClients->begin(); it != m_pConnectedClients->end(); ++it)
			{
				if (it->second.m_bIsActive == false) //No reply from client
				{
					itToDelete.push_back(it);
				}
			}

			//delete all who didn't reply
			for (size_t i = 0; i < itToDelete.size(); i++)
			{
				//Tell all living users a user disconnected
				std::string message = "<< " + itToDelete[i]->second.m_strName + " has left the server >>";
				TPacket _packet;
				_packet.Serialize(DATA, const_cast<char*>(message.c_str()));
				(*m_pConnectedClients).erase(itToDelete[i]); //delete

				SendDataToAllClients(_packet.PacketData);
			}



			m_keepAliveState = WAITING_NEXT_KEEP_ALIVE;
			m_lastKeepAliveTime = currentTime; //Set for next check up time
		}
		break;
	}
	}

}
