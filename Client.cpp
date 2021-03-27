#include <iostream>
#include <chrono>
#include <locale>
#include <queue>
#ifdef _WIN64
#define _WIN64_WINNT 0x0A00
#endif
#define ASIO_STANDALONE
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>


template <typename T>
struct head_of_message
{
	T id;
	int size = 0;
};
template <typename T>
struct message
{
	head_of_message <T> header{};
	std::vector<char> body;
	size_t size() const // size of Message
	{
		return sizeof(head_of_message<T>) + body.size();
	}
	template<typename DataType> // push data in buffer
	friend message <T>& operator << (message <T>& msg, const DataType& data)
	{
		size_t i = msg.body.size();

		msg.body.resize(msg.body.size() + sizeof(DataType));
		// change size of buffer
		std::memcpy(msg.body.data() + i, &data, sizeof(DataType));
		// copy in buffer
		msg.header.size = msg.size();
		// update size 
		return msg;
	}

      std::string GetBody(message <T>& msg) // method , which gives us a buffer
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
	client_side() : m_socket(m_context)
	{
	}

	bool Connect(const std::string& host, const uint16_t port)  // main function trying to establish a connection
	{
		try
		{
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
		if (IS_Connected)
		{
			m_connection->Disconnect;
		}
		m_context.stop();
		if (thrContext.joinable()) thrContext.join();
		m_connection.realese();
	}
	bool IS_Connected()
	{
		if (m_connection)
			return m_connection->IS_Connected();
		else
			return false;
	}
	void Send(const message<T>& msg)
	{
		if (IS_Connected())
			m_connection->Send(msg);
	}
	std::queue<owned_message<T>>& Incoming()
	{
		return queueIn;
	}

protected:
	asio::io_context m_context;
	std::thread thrContext;
	asio::ip::tcp::socket m_socket;
	asio::ip::tcp::endpoint m_endpoints;
	std::unique_ptr<connection <T> > m_connection;
	std::queue <owned_message<T> >  queueIn;
};
enum class Message_Types : uint32_t
{
	ServerMessage
};
class Client_service : public client_side<Message_Types>
{
public:
	void ServerMessage(std::string & str)
	{
		message<Message_Types> msg;
		msg.header.id = Message_Types::ServerMessage;
		msg << str;
		Send(msg);
	}
};
int main()
{
	std::string str;
	std::cout << "Please enter a string to send it to the server" << std::endl;
	std::getline(std::cin, str);
	Client_service c;
	c.Connect("127.0.0.1", 80);
	while (true)
	{
		if (str.size() != 0)
		{
			c.ServerMessage(str);
		}

		if (c.IS_Connected())
		{
			if (!c.Incoming().empty())
			{
				auto msg = c.Incoming().front().msg;
				switch (msg.header.id)
				{

				case Message_Types::ServerMessage:
				{
					std::cout << "You enter the string " << str << std::endl;
					str = "";
				}
				break;
				}
			}

		}
	}
	return 0;
}