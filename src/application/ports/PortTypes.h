#pragma once

#include <QString>
#include <QVector>

namespace devhub::application::ports {

enum class PortProtocol { Tcp, Udp };
enum class AddressFamily { IPv4, IPv6 };
enum class TcpState {
    Unknown, Closed, Listen, SynSent, SynReceived, Established, FinWait1, FinWait2,
    CloseWait, Closing, LastAck, TimeWait, DeleteTcb
};

struct ProcessIdentity final {
    quint32 processId = 0;
    quint64 creationTime = 0;
    friend bool operator==(const ProcessIdentity&, const ProcessIdentity&) = default;
};

struct PortRecord final {
    PortProtocol protocol = PortProtocol::Tcp;
    AddressFamily addressFamily = AddressFamily::IPv4;
    QString localAddress;
    quint16 localPort = 0;
    TcpState tcpState = TcpState::Unknown;
    ProcessIdentity process;
    QString processName;
    QString executablePath;
    QString processError;
    QString associatedWorkspaceId;
    QString associatedWorkspaceName;
};

struct ProcessDetails final {
    ProcessIdentity identity;
    QString processName;
    QString executablePath;
};

[[nodiscard]] QString protocolName(PortProtocol protocol);
[[nodiscard]] QString tcpStateName(TcpState state);

} // namespace devhub::application::ports
