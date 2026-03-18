#include "udpserver.h"
#include "ppbemulator.h"
#include <QNetworkDatagram>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QHostAddress>
UDPServer::UDPServer(PPBEmulator* emulator, quint16 port, QString address, QObject* parent)
    : QObject(parent), m_emulator(emulator), m_port(port), address(address)
{
    m_socket = new QUdpSocket(this);

    // Привязываемся к конкретному адресу ППБ1
    QHostAddress hostAddress("198.168.0.230");

    if (!m_socket->bind(hostAddress, m_port)) {
        qDebug() << "Ошибка привязки UDP сервера к" << hostAddress.toString() << ":" << m_port;
        qDebug() << "Ошибка:" << m_socket->errorString();
        return;
    }

    connect(m_socket, &QUdpSocket::readyRead,
            this, &UDPServer::readPendingDatagrams);

    connect(emulator, &PPBEmulator::sendPacketRequest,
            this, &UDPServer::onSendPacketRequest);

    qDebug() << QString("[%1] [INFO] ППБ1: UDP сервер запущен на %2:%3")
                    .arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"))
                    .arg(hostAddress.toString())
                    .arg(m_port);
}

UDPServer::~UDPServer()
{
    if (m_socket) {
        m_socket->close();
    }
}

void UDPServer::onSendPacketRequest(const QByteArray& packet, const QHostAddress& address, quint16 port)
{
    m_socket->writeDatagram(packet, address, port);
}

void UDPServer::readPendingDatagrams()
{
    while (m_socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        QByteArray temp = datagram.data();

         if (!datagram.isValid()) continue;

        QByteArray response = m_emulator->processPacket(
            datagram.data(),
            datagram.senderAddress(),
            datagram.senderPort()
            );

        if (!response.isEmpty()) {
            // Отправляем ответ целиком одним датаграммой
            m_socket->writeDatagram(response, datagram.senderAddress(), datagram.senderPort());

            QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
            qDebug().noquote() << QString("[%1] [INFO] Отправлен ответ (%2 байт)").arg(timestamp).arg(response.size());
        }
    }
}

void UDPServer::log(const QString& message, const QString& level)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString logMessage = QString("[%1] [%2] %3").arg(timestamp, level, message);
    qDebug().noquote() << logMessage;
}
