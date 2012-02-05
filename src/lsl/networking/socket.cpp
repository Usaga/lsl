#include "socket.h"

#include <lslutils/net.h>
#include <lslutils/conversion.h>

#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <sstream>

namespace BA = boost::asio;
namespace BS = boost::system;
namespace IP = boost::asio::ip;

namespace LSL {

Socket::Socket()
    : m_sock(m_netservice)
    , m_rate(-1)
    , m_last_net_packet(0)
{
}

void Socket::Connect(const std::string &server, int port)
{
    m_last_net_packet = 0;
    boost::system::error_code err;
    IP::address tempAddr = IP::address::from_string(server, err);
    if (err)
    {
        // error, maybe a hostname?
        IP::tcp::resolver resolver(m_netservice);
        std::ostringstream portbuf;
        portbuf << port;
        IP::tcp::resolver::query query(server, portbuf.str());
        IP::tcp::resolver::iterator iter = resolver.resolve(query, err);
        if (err)
        {
            sig_doneConnecting(false, err.message());
            return;
        }
        tempAddr = iter->endpoint().address();
    }
    IP::tcp::endpoint serverep(tempAddr, port);
    m_sock.async_connect(serverep, boost::bind(&Socket::ConnectCallback, this, BA::placeholders::error));
}

void Socket::ConnectCallback(const boost::system::error_code &error)
{
    if (!error)
    {
        sig_doneConnecting(true, "");
        BA::async_read_until(m_sock, m_incoming_buffer, "\n", boost::bind(&Socket::ReceiveCallback, this, BA::placeholders::error, BA::placeholders::bytes_transferred));
    }
    else
    {
        sig_doneConnecting(false, error.message());
    }
}

void Socket::ReceiveCallback(const boost::system::error_code &error, size_t bytes)
{
    m_last_net_packet = time( 0 );
    if (!error)
    {
        std::string msg;
        std::string command;
        std::istream buf(&m_incoming_buffer);
        buf >> command;
        std::getline(buf, msg);
        if (!msg.empty())
            msg = msg.substr(1);
        //emits the signal
        sig_dataReceived(command, msg);
    }
    else
    {
        if (error.value() == BS::errc::connection_reset || error.value() == BA::error::eof)
        {
            m_sock.close();
            sig_socketDisconnected();
        }
        else if (m_sock.is_open()) //! ignore error messages after connect was closed
        {
            sig_networkError(error.message());
        }
    }
    if (m_sock.is_open())
    {
        BA::async_read_until(m_sock, m_incoming_buffer, "\n", boost::bind(&Socket::ReceiveCallback, this, BA::placeholders::error, BA::placeholders::bytes_transferred));
    }
}

std::string Socket::GetHandle() const
{
    std::string handle;
    #ifdef WIN32

    IP_ADAPTER_INFO AdapterInfo[16];       // Allocate information for 16 cards
    DWORD dwBufLen = sizeof(AdapterInfo);  // Save memory size of buffer

    DWORD dwStatus = GetAdaptersInfo ( AdapterInfo, &dwBufLen); // Get info
        if (dwStatus != NO_ERROR) return ""; // Check status
    for (unsigned int i=0; i<std::min( (unsigned int)6, (unsigned int)AdapterInfo[0].AddressLength); i++)
    {
        handle += Util::ToString(((unsigned int)AdapterInfo[0].Address[i])&255);
        if (i != 5) handle += \1;
    }
    #elif defined(linux)
    int sock = socket (AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        return ""; //not a valid socket
    }
    struct ifreq dev; //container for the hw data
    struct if_nameindex *NameList = if_nameindex(); //container for the interfaces list
    if (NameList == NULL)
    {
        close(sock);
        return ""; //cannot list the interfaces
    }

    int pos = 0;
    std::string InterfaceName;
    do
    {
        if (NameList[pos].if_index == 0)
        {
            close(sock);
            if_freenameindex(NameList);
            return ""; // no valid interfaces found
        }
        InterfaceName = NameList[pos].if_name;
        pos++;
    } while (InterfaceName.substr(0,2) == "lo" || InterfaceName.substr(0,3) == "sit");

    if_freenameindex(NameList); //free the memory

    strcpy (dev.ifr_name, InterfaceName.c_str()); //select from the name
    if (ioctl(sock, SIOCGIFHWADDR, &dev) < 0) //get the interface data
    {
        close(sock);
        return ""; //cannot list the interfaces
    }

    for (int i=0; i<6; i++)
    {
        handle += Util::ToString(((unsigned int)dev.ifr_hwaddr.sa_data[i])&255);
        if (i != 5) handle += ':';
    }
    close(sock);
#endif
    return handle;

}

bool Socket::InTimeout(int timeout_seconds) const
{
    time_t now = time( 0 );
    return ( ( m_last_net_packet > 0 ) && ( ( now - m_last_net_packet ) > timeout_seconds ) );
}

std::string Socket::GetLocalAddress() const
{
    assert( false );
    return std::string();
}

void Socket::SetSendRateLimit(int Bps)
{
    m_rate = Bps;
}

Enum::SocketState Socket::State() const
{
	return Enum::SS_Open;
}

bool Socket::SendData(const std::string &msg)
{
	if (m_sock.is_open())
	{
		return m_sock.send(boost::asio::buffer(msg));
	}
	return 0;
}

} // namespace LSL
