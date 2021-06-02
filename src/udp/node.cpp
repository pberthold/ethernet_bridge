#include "node.h"
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <ethernet_msgs/Event.h>
#include <ethernet_msgs/EventType.h>
#include <ethernet_msgs/ProtocolType.h>
#include <ethernet_msgs/utils.h>


Node::Node(ros::NodeHandle& node_handle) : ros_handle_(node_handle)
{
    /// Parameter
    // Topics
    node_handle.param<std::string>("topic_busToHost", configuration_.topic_busToHost, "bus_to_host");
    node_handle.param<std::string>("topic_hostToBus", configuration_.topic_hostToBus, "host_to_bus");
    node_handle.param<std::string>("topic_event", configuration_.topic_event, "event");
    node_handle.param<std::string>("frame", configuration_.frame, "");
    node_handle.param<std::string>("ethernet_bindAddress", configuration_.ethernet_bindAddress, "0.0.0.0");
    node_handle.param<int>("ethernet_bindPort", configuration_.ethernet_bindPort, 55555);

    /// Subscribing & Publishing
    subscriber_ethernet_ = ros_handle_.subscribe(configuration_.topic_hostToBus, 100, &Node::rosCallback_ethernet, this);
    publisher_ethernet_packet_ = ros_handle_.advertise<ethernet_msgs::Packet>(configuration_.topic_busToHost, 100);
    publisher_ethernet_event_ = ros_handle_.advertise<ethernet_msgs::Event>(configuration_.topic_event, 100, true);

    /// Initialize socket
    socket_ = new QUdpSocket(this);
    connect(socket_, SIGNAL(readyRead()), this, SLOT(slotEthernetNewData()));
    connect(socket_, SIGNAL(connected()), this, SLOT(slotEthernetDisconnected()));
    connect(socket_, SIGNAL(disconnected()), this, SLOT(slotEthernetConnected()));
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    connect(socket_, SIGNAL(errorOccured(QAbstractSocket::SocketError)), this, SLOT(slotEthernetError(QAbstractSocket::SocketError)));
#endif

    // Don't share port (Qt-Socket)
//  bool success = socket_->bind(QHostAddress(QString::fromStdString(configuration_.ethernet_bindAddress)), configuration_.ethernet_bindPort, QAbstractSocket::DontShareAddress);

    // Share port (Qt-Socket)
    bool success = socket_->bind(QHostAddress(QString::fromStdString(configuration_.ethernet_bindAddress)), configuration_.ethernet_bindPort, QAbstractSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    // Share port (native Linux socket)
//  #include <sys/socket.h>
//  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
//  int optval = 1;
//  setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (void *) &optval, sizeof(optval));
//  socket_->setSocketDescriptor(sockfd, QUdpSocket::UnconnectedState);
//  bool success = socket_->bind(QHostAddress(QString::fromStdString(configuration_.ethernet_bindAddress)), configuration_.ethernet_bindPort, QAbstractSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    slotEthernetDisconnected();
    ROS_INFO("Binding to %s:%u -> %s", QHostAddress(QString::fromStdString(configuration_.ethernet_bindAddress)).toString().toLatin1().data(), configuration_.ethernet_bindPort, success?"ok":"failed");
}

Node::~Node()
{
    delete socket_;
}

void Node::rosCallback_ethernet(const ethernet_msgs::Packet::ConstPtr &msg)
{
    QNetworkDatagram datagram(QByteArray(reinterpret_cast<const char*>(msg->payload.data()), msg->payload.size()), QHostAddress(ethernet_msgs::nativeIp4ByArray(msg->receiver_ip)), msg->receiver_port);
    if (ethernet_msgs::nativeIp4ByArray(msg->sender_ip) != 0)
        datagram.setSender(QHostAddress(ethernet_msgs::nativeIp4ByArray(msg->sender_ip)), msg->sender_port);

    socket_->writeDatagram(datagram);
}

void Node::slotEthernetNewData()
{
    while (socket_->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = socket_->receiveDatagram();
        //  ethernet_msgs::Packet packet; // moved to private member for optimization

        packet.header.stamp = ros::Time::now();
        packet.header.frame_id = configuration_.frame;

        packet.sender_ip = ethernet_msgs::arrayByNativeIp4(datagram.senderAddress().toIPv4Address());
        packet.sender_port = datagram.senderPort();
        packet.receiver_ip = ethernet_msgs::arrayByNativeIp4(datagram.destinationAddress().toIPv4Address());
        packet.receiver_port = datagram.destinationPort();

        packet.payload.clear();
        packet.payload.reserve(datagram.data().count());
        std::copy(datagram.data().constBegin(), datagram.data().constEnd(), std::back_inserter(packet.payload));

        publisher_ethernet_packet_.publish(packet);
    }
}

void Node::slotEthernetConnected()
{
    ethernet_msgs::Event event;

    event.header.stamp = ros::Time::now();
    event.header.frame_id = configuration_.frame;

    event.type = ethernet_msgs::EventType::CONNECTED;

    publisher_ethernet_event_.publish(event);
}

void Node::slotEthernetDisconnected()
{
    ethernet_msgs::Event event;

    event.header.stamp = ros::Time::now();
    event.header.frame_id = configuration_.frame;

    event.type = ethernet_msgs::EventType::DISCONNECTED;

    publisher_ethernet_event_.publish(event);
}

void Node::slotEthernetError(int error_code)
{
    ethernet_msgs::Event event;

    event.header.stamp = ros::Time::now();
    event.header.frame_id = configuration_.frame;

    event.type = ethernet_msgs::EventType::SOCKETERROR;
    event.value = error_code;

    publisher_ethernet_event_.publish(event);
}