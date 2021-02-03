#ifndef PACKET_H
#define PACKET_H


#include <QByteArray>

class Packet : public QByteArray
{
public:
    enum AppSize {
        ReceiveDataSize = 162,
        SendDataSize = 102
    };

    Packet(quint16 size);
    Packet(const QByteArray &packet);

    quint8 *appData() const;

    bool isValid() const;
};

#endif // PACKET_H
