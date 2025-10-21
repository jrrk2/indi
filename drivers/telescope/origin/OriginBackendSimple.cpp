#include "OriginBackendSimple.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <QEventLoop>
#include <cmath>

OriginBackendSimple::OriginBackendSimple()
    : m_webSocket(new SimpleWebSocket())
    , m_networkManager(new QNetworkAccessManager())
    , m_connectedPort(80)
    , m_connected(false)
    , m_logicallyConnected(false)
    , m_cameraConnected(false)
    , m_nextSequenceId(2000)
{
}

OriginBackendSimple::~OriginBackendSimple()
{
    disconnectFromTelescope();
    delete m_webSocket;
    delete m_networkManager;
}

bool OriginBackendSimple::connectToTelescope(const QString& host, int port)
{
    m_connectedHost = host;
    m_connectedPort = port;
    
    std::string path = "/SmartScope-1.0/mountControlEndpoint";
    
    qDebug() << "Connecting to Origin at" << host << ":" << port;
    
    if (!m_webSocket->connect(host.toStdString(), port, path))
    {
        qWarning() << "Failed to connect WebSocket";
        return false;
    }
    
    m_connected = true;
    qDebug() << "WebSocket connected";
    
    // Send initial status request
    sendCommand("GetStatus", "Mount");
    
    return true;
}

void OriginBackendSimple::disconnectFromTelescope()
{
    if (m_webSocket)
    {
        m_webSocket->disconnect();
    }
    m_connected = false;
    m_logicallyConnected = false;
}

void OriginBackendSimple::poll()
{
    if (!m_connected || !m_webSocket)
        return;
    
    // Check for incoming messages
    while (m_webSocket->hasData())
    {
        std::string message = m_webSocket->receiveText();
        if (!message.empty())
        {
            processMessage(message);
        }
    }
}

void OriginBackendSimple::processMessage(const std::string& message)
{
    QString qmsg = QString::fromStdString(message);
    QJsonDocument doc = QJsonDocument::fromJson(qmsg.toUtf8());
    
    if (!doc.isObject())
        return;
    
    QJsonObject obj = doc.object();
    
    // Parse telescope data
    QString source = obj["Source"].toString();
    
    if (source == "Mount")
    {
        // Update mount status
        if (obj.contains("Ra"))
            m_status.raPosition = radiansToHours(obj["Ra"].toDouble());
        if (obj.contains("Dec"))
            m_status.decPosition = radiansToDegrees(obj["Dec"].toDouble());
        if (obj.contains("IsTracking"))
            m_status.isTracking = obj["IsTracking"].toBool();
        if (obj.contains("IsGotoOver"))
            m_status.isSlewing = !obj["IsGotoOver"].toBool();
        
        // Call status callback
        if (m_statusCallback)
            m_statusCallback();
    }
    
    // Handle image notifications
    QString command = obj["Command"].toString();
    QString type = obj["Type"].toString();
    
    if (source == "ImageServer" && command == "NewImageReady" && type == "Notification")
    {
        QString filePath = obj["FileLocation"].toString();
        if (!filePath.isEmpty() && filePath.endsWith(".tiff", Qt::CaseInsensitive))
        {
            requestImage(filePath);
        }
    }
}

void OriginBackendSimple::sendCommand(const QString& command, const QString& destination,
                                     const QJsonObject& params)
{
    QJsonObject jsonCommand;
    jsonCommand["Command"] = command;
    jsonCommand["Destination"] = destination;
    jsonCommand["SequenceID"] = m_nextSequenceId++;
    jsonCommand["Source"] = "INDIDriver";
    jsonCommand["Type"] = "Command";
    
    // Add parameters
    for (auto it = params.begin(); it != params.end(); ++it)
    {
        jsonCommand[it.key()] = it.value();
    }
    
    QJsonDocument doc(jsonCommand);
    QString msgStr = doc.toJson(QJsonDocument::Compact);
    
    if (m_webSocket && m_webSocket->isConnected())
    {
        m_webSocket->sendText(msgStr.toStdString());
        qDebug() << "Sent:" << msgStr;
    }
}

bool OriginBackendSimple::gotoPosition(double ra, double dec)
{
    QJsonObject params;
    params["Ra"] = hoursToRadians(ra);
    params["Dec"] = degreesToRadians(dec);
    
    sendCommand("GotoRaDec", "Mount", params);
    return true;
}

bool OriginBackendSimple::syncPosition(double ra, double dec)
{
    QJsonObject params;
    params["Ra"] = hoursToRadians(ra);
    params["Dec"] = degreesToRadians(dec);
    
    sendCommand("SyncToRaDec", "Mount", params);
    return true;
}

bool OriginBackendSimple::abortMotion()
{
    sendCommand("AbortAxisMovement", "Mount");
    return true;
}

bool OriginBackendSimple::parkMount()
{
    sendCommand("Park", "Mount");
    return true;
}

bool OriginBackendSimple::unparkMount()
{
    sendCommand("Unpark", "Mount");
    return true;
}

bool OriginBackendSimple::setTracking(bool enabled)
{
    QString command = enabled ? "StartTracking" : "StopTracking";
    sendCommand(command, "Mount");
    return true;
}

bool OriginBackendSimple::isTracking() const
{
    return m_status.isTracking;
}

bool OriginBackendSimple::takeSnapshot(double exposure, int iso)
{
    QJsonObject params;
    params["ExposureTime"] = exposure;
    params["ISO"] = iso;
    
    sendCommand("RunSampleCapture", "TaskController", params);
    return true;
}

bool OriginBackendSimple::abortExposure()
{
    return true;
}

void OriginBackendSimple::requestImage(const QString& filePath)
{
    QString fullPath = QString("http://%1/SmartScope-1.0/dev2/%2")
                       .arg(m_connectedHost, filePath);
    
    QUrl url(fullPath);
    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);
    
    QObject::connect(reply, &QNetworkReply::finished, [this, reply, filePath]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            QByteArray data = reply->readAll();
            
            // Call image callback instead of emitting signal
            if (m_imageCallback)
            {
                m_imageCallback(filePath, data, 0, 0, 0);
            }
        }
        reply->deleteLater();
    });
}

double OriginBackendSimple::hoursToRadians(double hours)
{
    return hours * M_PI / 12.0;
}

double OriginBackendSimple::degreesToRadians(double degrees)
{
    return degrees * M_PI / 180.0;
}

double OriginBackendSimple::radiansToHours(double radians)
{
    return radians * 12.0 / M_PI;
}

double OriginBackendSimple::radiansToDegrees(double radians)
{
    return radians * 180.0 / M_PI;
}
