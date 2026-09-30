#include "WindowsPortProvider.h"

#include "../../domain/errors/AppError.h"

#include <QHostAddress>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#endif

namespace devhub::infrastructure::ports {
namespace {
using namespace application::ports;

#ifndef Q_OS_WIN
shared::common::Result<QVector<PortRecord>> apiFailure(const QString& operation, quint32 code)
{
    return shared::common::Result<QVector<PortRecord>>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Port,
        QStringLiteral("DevHub could not read the Windows port table."),
        QStringLiteral("%1 failed with Windows error %2.").arg(operation).arg(code), operation));
}
#endif

PortRecord makeRecord(PortProtocol protocol, AddressFamily family, QString address, quint16 port,
                      TcpState state, quint32 processId)
{
    PortRecord record;
    record.protocol = protocol;
    record.addressFamily = family;
    record.localAddress = std::move(address);
    record.localPort = port;
    record.tcpState = state;
    record.process.processId = processId;
    return record;
}

QString ipv6Address(const UCHAR* bytes)
{
    Q_IPV6ADDR address{};
    for (int index = 0; index < 16; ++index) address[index] = bytes[index];
    return QHostAddress(address).toString();
}

TcpState convertState(DWORD state)
{
    switch (state) {
    case MIB_TCP_STATE_CLOSED: return TcpState::Closed;
    case MIB_TCP_STATE_LISTEN: return TcpState::Listen;
    case MIB_TCP_STATE_SYN_SENT: return TcpState::SynSent;
    case MIB_TCP_STATE_SYN_RCVD: return TcpState::SynReceived;
    case MIB_TCP_STATE_ESTAB: return TcpState::Established;
    case MIB_TCP_STATE_FIN_WAIT1: return TcpState::FinWait1;
    case MIB_TCP_STATE_FIN_WAIT2: return TcpState::FinWait2;
    case MIB_TCP_STATE_CLOSE_WAIT: return TcpState::CloseWait;
    case MIB_TCP_STATE_CLOSING: return TcpState::Closing;
    case MIB_TCP_STATE_LAST_ACK: return TcpState::LastAck;
    case MIB_TCP_STATE_TIME_WAIT: return TcpState::TimeWait;
    case MIB_TCP_STATE_DELETE_TCB: return TcpState::DeleteTcb;
    default: return TcpState::Unknown;
    }
}

template<typename Table, typename Query>
shared::common::Result<QByteArray> readTable(Query query, const QString& operation)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        ULONG size = 0;
        DWORD status = query(nullptr, &size);
        if (status != ERROR_INSUFFICIENT_BUFFER && status != NO_ERROR) {
            return shared::common::Result<QByteArray>::failure(domain::errors::AppError::failure(
                domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Port,
                QStringLiteral("DevHub could not read the Windows port table."),
                QStringLiteral("%1 size query failed with Windows error %2.").arg(operation).arg(status), operation));
        }
        QByteArray buffer(static_cast<qsizetype>(size), Qt::Uninitialized);
        status = query(reinterpret_cast<Table*>(buffer.data()), &size);
        if (status == NO_ERROR) {
            buffer.resize(static_cast<qsizetype>(size));
            return shared::common::Result<QByteArray>::success(std::move(buffer));
        }
        if (status != ERROR_INSUFFICIENT_BUFFER) {
            return shared::common::Result<QByteArray>::failure(domain::errors::AppError::failure(
                domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Port,
                QStringLiteral("DevHub could not read the Windows port table."),
                QStringLiteral("%1 failed with Windows error %2.").arg(operation).arg(status), operation));
        }
    }
    return shared::common::Result<QByteArray>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::Port,
        QStringLiteral("The Windows port table kept changing. Try refreshing again."),
        QStringLiteral("%1 exhausted bounded buffer-resize retries.").arg(operation), operation));
}
}

shared::common::Result<QVector<application::ports::PortRecord>> WindowsPortProvider::enumerate()
{
#ifdef Q_OS_WIN
    using namespace application::ports;
    QVector<PortRecord> records;

    auto tcp4 = readTable<MIB_TCPTABLE_OWNER_PID>([](void* table, ULONG* size) {
        return GetExtendedTcpTable(table, size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    }, QStringLiteral("GetExtendedTcpTable IPv4"));
    if (!tcp4) return shared::common::Result<QVector<PortRecord>>::failure(tcp4.error());
    auto* v4 = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(tcp4.value().constData());
    for (DWORD i = 0; i < v4->dwNumEntries; ++i) {
        const auto& row = v4->table[i];
        records.append(makeRecord(PortProtocol::Tcp, AddressFamily::IPv4,
            QHostAddress(qFromBigEndian(row.dwLocalAddr)).toString(), ntohs(static_cast<u_short>(row.dwLocalPort)),
            convertState(row.dwState), row.dwOwningPid));
    }

    auto tcp6 = readTable<MIB_TCP6TABLE_OWNER_PID>([](void* table, ULONG* size) {
        return GetExtendedTcpTable(table, size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    }, QStringLiteral("GetExtendedTcpTable IPv6"));
    if (!tcp6) return shared::common::Result<QVector<PortRecord>>::failure(tcp6.error());
    auto* v6 = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(tcp6.value().constData());
    for (DWORD i = 0; i < v6->dwNumEntries; ++i) {
        const auto& row = v6->table[i];
        records.append(makeRecord(PortProtocol::Tcp, AddressFamily::IPv6, ipv6Address(row.ucLocalAddr),
            ntohs(static_cast<u_short>(row.dwLocalPort)), convertState(row.dwState), row.dwOwningPid));
    }

    auto udp4 = readTable<MIB_UDPTABLE_OWNER_PID>([](void* table, ULONG* size) {
        return GetExtendedUdpTable(table, size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    }, QStringLiteral("GetExtendedUdpTable IPv4"));
    if (!udp4) return shared::common::Result<QVector<PortRecord>>::failure(udp4.error());
    auto* u4 = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(udp4.value().constData());
    for (DWORD i = 0; i < u4->dwNumEntries; ++i) {
        const auto& row = u4->table[i];
        records.append(makeRecord(PortProtocol::Udp, AddressFamily::IPv4,
            QHostAddress(qFromBigEndian(row.dwLocalAddr)).toString(), ntohs(static_cast<u_short>(row.dwLocalPort)),
            TcpState::Unknown, row.dwOwningPid));
    }

    auto udp6 = readTable<MIB_UDP6TABLE_OWNER_PID>([](void* table, ULONG* size) {
        return GetExtendedUdpTable(table, size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    }, QStringLiteral("GetExtendedUdpTable IPv6"));
    if (!udp6) return shared::common::Result<QVector<PortRecord>>::failure(udp6.error());
    auto* u6 = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(udp6.value().constData());
    for (DWORD i = 0; i < u6->dwNumEntries; ++i) {
        const auto& row = u6->table[i];
        records.append(makeRecord(PortProtocol::Udp, AddressFamily::IPv6, ipv6Address(row.ucLocalAddr),
            ntohs(static_cast<u_short>(row.dwLocalPort)), TcpState::Unknown, row.dwOwningPid));
    }
    return shared::common::Result<QVector<PortRecord>>::success(std::move(records));
#else
    return apiFailure(QStringLiteral("Windows port enumeration"), ERROR_CALL_NOT_IMPLEMENTED);
#endif
}

} // namespace devhub::infrastructure::ports
