
#include <iostream>
#include <chrono>
#include <locale>
#include <queue>
#include <cctype>
#ifdef _WIN64
#define _WIN64_WINNT 0x0A00
#endif
#define ASIO_STANDALONE
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>

enum class Message_Types : uint32_t
{
	ServerMessage
};
template <typename T>
struct head_of_message
{
	T id;
	int size = 0;
};
template <typename T>
struct message
{
public:
	head_of_message <T> header{};
	std::vector<char> body;
	size_t size() const // // размер моего сообщения
	{
		return sizeof(head_of_message<T>) + body.size();
	}
	template<typename DataType> // помещение данных в мой буффер
	friend message <T>& operator << (message <T>& msg, const DataType& data)
	{
		size_t i = msg.body.size();

		msg.body.resize(msg.body.size() + sizeof(DataType));
		// изменить размер буфера
		std::memcpy(msg.body.data() + i, &data, sizeof(DataType));
		// копируем в буфер
		msg.header.size = msg.size();
		// обновляем размер 
		return msg;
	}

	std::string GetBody(message <T>& msg) // функция достающая данные из буфера
	{
		std::string res = "";
		std::vector<char> body_msg = msg.body;
		for (int i = 0; i < body_msg.size(); i++)
		{
			if (isalpha(body_msg[i]))
			res = res + body_msg[i];
		}
		return res;
	}
};


std::vector <char> vBuffer(1024);


template <typename T>
class connection;
template <typename T>
struct owned_message
{
	std::shared_ptr<connection <T> > remote = nullptr;
	message <T> msg;
	friend std::ostream& operator << (std::ostream& os, const owned_message <T>& msg)
	{
		os << msg.msg;
		return os;
	}
};
template <typename T>
class connection : public std::enable_shared_from_this<connection<T>>
{
public:

	enum class owner // the class that is needed to determine who owns the connection 
	{
		server,
		client
	};

public:

	connection(owner parent, asio::io_context& asioContext, asio::ip::tcp::socket socket, std::queue<owned_message<T>>& qIn)
		: m_asioContext(asioContext), m_socket(std::move(socket)), queueIn(qIn)
	{
		m_nOwnerType = parent;
	} // constructor takes values  parent - creator of connection, context, socket and queue - it is incoming messages


	int GetID() const
	{
		return id;
	} // take ID of client

public:
	void ConnectToClient(int uid = 0)
	{
		if (m_nOwnerType == owner::server) // if creator of our coonection is server then
		{
			if (m_socket.is_open())
			{
				id = uid;
				ReadHeader();
			}
		}
	}

	void ConnectToServer(const asio::ip::tcp::resolver::results_type& endpoints)
	{
		if (m_nOwnerType == owner::client) // if creator of our coonection is client then
		{
			// asio try to connecto to endpoint
			asio::async_connect(m_socket, endpoints,
				[this](std::error_code ec, asio::ip::tcp::endpoint endpoint)
				{
					if (!ec)
					{
						ReadHeader();
					}
				});
		}
	}


	void Disconnect()
	{
		if (IS_Connected()) // if we have connection then close our socket
			asio::post(m_asioContext, [this]() { m_socket.close(); });
	}

	bool IS_Connected() const
	{
		return m_socket.is_open(); // true - socket is open , false - socket is not open
	}


public:
	void Send(const message<T>& msg) // sending our message
	{
		asio::post(m_asioContext,
			[this, msg]()
			{
				bool flag = queueOut.empty(); // our queue is empty?
				queueOut.push(msg);
				if (flag) // if our queue is empty when start procces writing
				{
					WriteHeader();
				}
			});

	}



private:
	void WriteHeader()
	{
		asio::async_write(m_socket, asio::buffer(&queueOut.front().header, sizeof(head_of_message<T>)),
			[this](std::error_code ec, std::size_t length)
			{

				if (!ec)
				{
					if (queueOut.front().body.size() > 0)
					{
						WriteBody();
					}
					else
					{

						queueOut.front();
						if (!queueOut.empty())
						{
							WriteHeader();
						}
					}
				}
				else
				{
					std::cout << id;
					std::cout << " Write header fail";
					m_socket.close();
				}
			});
	}

	void WriteBody()
	{
		asio::async_write(m_socket, asio::buffer(queueOut.front().body.data(), queueOut.front().body.size()),
			[this](std::error_code ec, std::size_t length)
			{
				if (!ec)
				{
					queueOut.front();
					if (!queueOut.empty())
					{
						WriteHeader();
					}
				}
				else
				{
					std::cout << id;
					std::cout << " Wrute body fail" << std::endl;
					m_socket.close();
				}
			});
	}

	void ReadHeader()
	{
		asio::async_read(m_socket, asio::buffer(&tempIn.header, sizeof(head_of_message<T>)),
			[this](std::error_code ec, std::size_t length)
			{
				if (!ec)
				{
					if (tempIn.header.size > 0)
					{
						tempIn.body.resize(tempIn.header.size);
						ReadBody();
					}
					else
					{
						AddToIncomingMessageQueue();
					}
				}
				else
				{
					std::cout << id;
					std::cout << " Read header fail" << std::endl;
					m_socket.close();
				}
			});
	}

	void ReadBody()
	{
		asio::async_read(m_socket, asio::buffer(tempIn.body.data(), tempIn.body.size()),
			[this](std::error_code ec, std::size_t length)
			{
				if (!ec)
				{
					AddToIncomingMessageQueue();
				}
				else
				{
					std::cout << id;
					std::cout << "Read body fail" << std::endl;
					m_socket.close();
				}
			});
	}

	void AddToIncomingMessageQueue()
	{

		if (m_nOwnerType == owner::server)
			queueIn.push({ this->shared_from_this(), tempIn });
		else
			queueIn.push({ nullptr, tempIn });

		ReadHeader();
	}

protected:

	asio::ip::tcp::socket m_socket;
	asio::io_context& m_asioContext;
	std::queue<message<T>> queueOut;
	std::queue<owned_message<T>>& queueIn;
	message<T> tempIn;
	owner m_nOwnerType = owner::server;
	int id = 0;

};

template <typename T>
class client_side
{
public:
	bool Connect(const std::string& host, const uint16_t port)
	{
		try
		{
			m_connection = std::make_unique<connection<T>>();
			asio::ip::tcp::resolver resolver(m_context);
			asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));
			m_connection = std::make_unique<connection<T>>(connection<T>::owner::client, m_context, asio::ip::tcp::socket(m_context), queueIn);

			m_connection->ConnectToServer(endpoints);
			thrContext = std::thread([&]() { m_context.run(); });

		}
		catch (std::exception ex)
		{
			std::cout << "sorry we have an error : " << ex.what() << std::endl;
			return false;
		}
		return true;
	}
	void Disconnect()
	{
		if (IS_Connected())
		{
			m_connection->Disconnect;
		}
		m_context.stop();
		if (thrContext.joinable()) thrContext.join();
		m_connection.realese();
	}
	bool IS_Connected()
	{

	}

protected:
	asio::io_context m_context;
	std::thread thrContext;
	asio::ip::tcp::socket m_socket;
	asio::ip::tcp::endpoint m_endpoints;
	std::unique_ptr<connection <T> > m_connection;
private:
	std::queue < message<T> >  queueIn;
};

template <typename T>
class server_side
{
public:
	server_side(uint16_t port) :m_asioAcceptor(m_asioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
	{

	}
	bool Start()
	{
		try
		{
			WaitForClientConnection();
			m_threadContext = std::thread([this]() {m_asioContext.run();  });
		}
		catch (std::exception & ex)
		{
			std::cout << "Server exception : " << ex.what() << std::endl;
			return false;
		}
		std::cout << "Now, server is working " << std::endl;
		return true;
	}
	void Stop()
	{
		m_asioContext.stop();
		if (m_threadContext.joinable()) m_threadContext.join();
		std::cout << "Server stopped" << std::endl;
	}
	void WaitForClientConnection()
	{
		m_asioAcceptor.async_accept(
			[this](std::error_code ec, asio::ip::tcp::socket socket)
			{
				if (!ec)
				{
					std::cout << "Connected, IP - address :" << socket.remote_endpoint() << std::endl;
						std::shared_ptr <connection <T>>  newconn =
							std::make_shared <connection <T> >(connection<T>::owner::server,
								m_asioContext, std::move(socket), queueIn);
						if (OnClientConnect(newconn))
						{
							m_deqConnections.push_back(std::move(newconn));
							m_deqConnections.back()->ConnectToClient(nIDCOUNTER++);
							std::cout << m_deqConnections.back()->GetID();
							std::cout << " computer ";
							std::cout << "There is a connection with server" << std::endl;
						}
				}
				else
				{
					std::cout << "New connection error : " << ec.message() << std::endl;
				}
				WaitForClientConnection();
			});
	}
	void MessageClient(std::shared_ptr<connection <T> > client, const message <T>& msg) // sena a message to special client
	{
		if (client && client->IsConnected())
		{
			client->Send(msg);
			client.reset();
		}
		else
		{
			OnClientDisconnect(client);
		}
	}
public:
	void Update()
	{
		while (!queueIn.empty())
		{
			auto msg = queueIn.front();
			OnMessage(msg.remote, msg.msg);
		}

	}
protected:
	virtual bool OnClientConnect(std::shared_ptr<connection <T> > client)
	{
		return false;
	}
	virtual void OnMessage(std::shared_ptr<connection <T> > client, message <T>& msg)
	{
	}
protected:
	std::queue<owned_message<T>> queueIn;
	asio::io_context m_asioContext;
	std::thread m_threadContext;
	asio::ip::tcp::acceptor m_asioAcceptor;
	uint32_t nIDCOUNTER = 1;
	std::deque < std::shared_ptr <connection <T>> > m_deqConnections;
};

class Server_service :public server_side<Message_Types>
{
public:
	Server_service(uint16_t nPort) : server_side<Message_Types>(nPort)
	{

	}
	virtual bool OnClientConnect(std::shared_ptr<connection<Message_Types>> client)
	{
		return true;
	}
	virtual void OnMessage(std::shared_ptr<connection<Message_Types>> client, message<Message_Types>& msg) {
		switch (msg.header.id)
		{
		case Message_Types::ServerMessage:
		{
			std::cout << client->GetID();
			std::cout << "  ";
			std::cout << msg.GetBody(msg) << std::endl;
			client->Send(msg);
		}
		break;
		}
	}
};
int main()
{
	setlocale(LC_ALL, "Russian");
	Server_service serv(80);
	serv.Start();
	while (1)
	{
		serv.Update();
	}
	return 0;
}