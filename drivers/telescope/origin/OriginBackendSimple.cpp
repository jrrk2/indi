#include "OriginBackendSimple.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <cmath>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

OriginBackendSimple::OriginBackendSimple()
    : m_webSocket(new SimpleWebSocket())
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
    int messageCount = 0;
    while (m_webSocket->hasData())
    {
        std::string message = m_webSocket->receiveText();
        if (!message.empty())
        {
            messageCount++;
            if (false) qDebug() << "RECEIVED MESSAGE:" << QString::fromStdString(message);
            processMessage(message);
        }
    }
    
    if (messageCount > 0)
    {
      if (false) qDebug() << "Processed" << messageCount << "messages";
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
    if (false) qDebug() << "Processing message from:" << source;

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
    qDebug() << "Image notification received:" << filePath;
    
    // Store the path for later download
    m_pendingImagePath = filePath;
    
    // Download the image using simple HTTP GET without QEventLoop
    QString fullPath = QString("http://%1/SmartScope-1.0/dev2/%2")
                       .arg(m_connectedHost, filePath);
    
    qDebug() << "Will download from:" << fullPath;
    
    // Use curl or simple HTTP download
    QByteArray imageData = downloadImageSync(fullPath);
    
    if (!imageData.isEmpty())
    {
        qDebug() << "Downloaded" << imageData.size() << "bytes";
        
        // Call image callback with the data
        if (m_imageCallback)
        {
            m_imageCallback(filePath, imageData, 0, 0, 0);
        }
    }
    else
    {
        qDebug() << "Failed to download image";
    }
}


// Add this helper function to OriginBackendSimple.cpp
QByteArray OriginBackendSimple::downloadImageSync(const QString& url)
{
    QByteArray result;
    
    // Parse URL
    QUrl qurl(url);
    QString host = qurl.host();
    int port = qurl.port(80);
    QString path = qurl.path();
    
    qDebug() << "Downloading from host:" << host << "port:" << port << "path:" << path;
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        qDebug() << "Failed to create socket";
        return result;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Resolve host
    struct hostent *server = gethostbyname(host.toUtf8().constData());
    if (server == nullptr)
    {
        qDebug() << "Failed to resolve host";
        close(sock);
        return result;
    }
    
    // Connect
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        qDebug() << "Failed to connect to server";
        close(sock);
        return result;
    }
    
    // Send HTTP GET request
    QString request = QString("GET %1 HTTP/1.1\r\n"
                             "Host: %2\r\n"
                             "Connection: close\r\n"
                             "\r\n").arg(path, host);
    
    qDebug() << "Sending HTTP request";
    send(sock, request.toUtf8().constData(), request.length(), 0);
    
    // Read response
    char buffer[4096];
    QByteArray response;
    int bytesRead;
    
    while ((bytesRead = recv(sock, buffer, sizeof(buffer), 0)) > 0)
    {
        response.append(buffer, bytesRead);
    }
    
    close(sock);
    
    qDebug() << "Received" << response.size() << "bytes total";
    
    // Parse HTTP response - find end of headers
    int headerEnd = response.indexOf("\r\n\r\n");
    if (headerEnd > 0)
    {
        // Extract body (the image data)
        result = response.mid(headerEnd + 4);
        qDebug() << "Image data size:" << result.size() << "bytes";
    }
    else
    {
        qDebug() << "Failed to parse HTTP response";
    }
    
    return result;
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
