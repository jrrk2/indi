#include "OriginBackendSimple.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <cmath>

OriginBackendSimple::OriginBackendSimple(QObject *parent)
    : QObject(parent)
    , m_webSocket(new SimpleWebSocket())
    , m_dataProcessor(new TelescopeDataProcessor(this))
    , m_networkManager(new QNetworkAccessManager(this))
    , m_connectedPort(80)
    , m_connected(false)
    , m_logicallyConnected(false)
    , m_cameraConnected(false)
    , m_nextSequenceId(2000)
{
    // Connect data processor signals
    connect(m_dataProcessor, &TelescopeDataProcessor::mountStatusUpdated,
            this, &OriginBackendSimple::statusUpdated);
}

OriginBackendSimple::~OriginBackendSimple()
{
    disconnectFromTelescope();
    delete m_webSocket;
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
    
    // Process through data processor
    m_dataProcessor->processJsonPacket(qmsg.toUtf8());
    
    // Update our status from processor
    const TelescopeData& data = m_dataProcessor->getData();
    m_status.raPosition = radiansToHours(data.mount.enc0);
    m_status.decPosition = radiansToDegrees(data.mount.enc1);
    m_status.isTracking = data.mount.isTracking;
    m_status.isSlewing = !data.mount.isGotoOver;
    m_status.temperature = data.environment.ambientTemperature;
    
    // Handle image notifications
    QString source = obj["Source"].toString();
    QString command = obj["Command"].toString();
    QString type = obj["Type"].toString();
    
    if (source == "ImageServer" && command == "NewImageReady" && type == "Notification")
    {
        QString filePath = obj["FileLocation"].toString();
        if (!filePath.isEmpty() && filePath.endsWith(".tiff", Qt::CaseInsensitive))
        {
            double ra = obj["Ra"].toDouble();
            double dec = obj["Dec"].toDouble();
            double exposure = obj["ExposureTime"].toDouble();
            
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
    QString message = doc.toJson(QJsonDocument::Compact);
    
    if (m_webSocket && m_webSocket->isConnected())
    {
        m_webSocket->sendText(message.toStdString());
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
    return true; // Origin doesn't support abort
}

void OriginBackendSimple::requestImage(const QString& filePath)
{
    QString fullPath = QString("http://%1/SmartScope-1.0/dev2/%2")
                       .arg(m_connectedHost, filePath);
    
    QUrl url(fullPath);  // CREATE QUrl FIRST
    QNetworkRequest request(url);  // THEN CREATE REQUEST
    QNetworkReply *reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, [this, reply, filePath]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            QByteArray data = reply->readAll();
            emit tiffImageDownloaded(filePath, data, 0, 0, 0);
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
