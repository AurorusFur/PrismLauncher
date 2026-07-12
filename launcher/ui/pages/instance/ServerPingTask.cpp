#include <QFutureWatcher>

#include <Json.h>
#include "Exception.h"
#include "McClient.h"
#include "McResolver.h"
#include "ServerPingTask.h"


void ServerPingTask::executeTask()
{
    qDebug() << "Querying status of" << QString("%1:%2").arg(m_domain).arg(m_port);

    // Resolve the actual IP and port for the server
    McResolver* resolver = new McResolver(nullptr, m_domain, m_port);
    connect(resolver, &McResolver::succeeded, this, [this](QString ip, int port) {
        qDebug().nospace().noquote() << "Resolved address for " << m_domain << ": " << ip << ":" << port;

        // Now that we have the IP and port, query the server
        McClient* client = new McClient(nullptr, m_domain, ip, port);

        connect(client, &McClient::succeeded, this, [this](QJsonObject data) {
            // A response without players.online is not "0 players" — fail so the
            // page can show the server as unreachable instead.
            try {
                m_outputOnlinePlayers = Json::requireInteger(Json::requireObject(data, "players"), "online");
                qDebug() << "Online players:" << m_outputOnlinePlayers;
                emitSucceeded();
            } catch (Exception& e) {
                qWarning() << "server ping failed to parse response" << e.what();
                emitFailed(e.what());
            }
        });
        connect(client, &McClient::failed, this, [this](QString error) { emitFailed(error); });

        // Delete McClient object when done
        connect(client, &McClient::finished, this, [client]() { client->deleteLater(); });
        client->getStatusData();
    });
    connect(resolver, &McResolver::failed, this, [this](QString error) { emitFailed(error); });

    // Delete McResolver object when done
    connect(resolver, &McResolver::finished, [resolver]() { resolver->deleteLater(); });
    resolver->ping();
}
