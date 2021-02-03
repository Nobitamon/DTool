#include "packet.h"

#define LEN_CHECKDATA  4

Packet::Packet(quint16 size)
    : QByteArray(size, 0)
{
}

Packet::Packet(const QByteArray &packet)
    : QByteArray(packet)
{
}

quint8 *Packet::appData() const
{
    return (quint8 *)data();
}

bool Packet::isValid() const
{
    const quint8 *d = (quint8 *)data();

    /* check size */
    int sz = size();
    if (sz != Packet::ReceiveDataSize)
        return false;

    /* check header */
    if (d[0] != 0x55 || d[1] != 0xAA || d[2] != 0xAA || d[3] != 0x55)
        return false;


    return true;
}
