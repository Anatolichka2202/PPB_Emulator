#ifndef UDPSERVER_H
#define UDPSERVER_H

#include <QObject>
#include <QUdpSocket>

class PPBEmulator;

class UDPServer : public QObject {
    Q_OBJECT
public:
    explicit UDPServer(PPBEmulator* emulator, quint16 port = 0, QString address = 0, QObject* parent = nullptr);
    ~UDPServer();

private slots:
    void readPendingDatagrams();
    void onSendPacketRequest(const QByteArray& packet, const QHostAddress& address, quint16 port);

private:
    QUdpSocket* m_socket;
    PPBEmulator* m_emulator;
    quint16 m_port;
    void log(const QString& message, const QString& level);
    QString address;
};

#endif // UDPSERVER_H
