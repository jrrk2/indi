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

// In OriginBackendSimple.cpp

void OriginBackendSimple::requestImage(const QString& filePath)
{
    qDebug() << "=== IMAGE DOWNLOAD START ===" << QDateTime::currentDateTime().toString();
    qDebug() << "Image notification received:" << filePath;
    
    // Store the path for later download
    m_pendingImagePath = filePath;
    
    // Download the image using simple HTTP GET without QEventLoop
    QString fullPath = QString("http://%1/SmartScope-1.0/dev2/%2")
                       .arg(m_connectedHost, filePath);
    
    qDebug() << "Will download from:" << fullPath;
    
    // Record start time
    auto startTime = std::chrono::steady_clock::now();
    
    // Use synchronous download
    QByteArray imageData = downloadImageSync(fullPath);
    
    // Record end time
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    qDebug() << "=== IMAGE DOWNLOAD COMPLETE ===" << QDateTime::currentDateTime().toString();
    qDebug() << "Download took:" << duration << "ms (" << (duration/1000.0) << "seconds)";
    
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
    
    qDebug() << "=== IMAGE PROCESSING COMPLETE ===" << QDateTime::currentDateTime().toString();
}

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
    
    // Set socket timeouts - increase for large files
    struct timeval timeout;
    timeout.tv_sec = 60;  // 60 second timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
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
    
    qDebug() << "Connecting to server...";
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        qDebug() << "Failed to connect to server";
        close(sock);
        return result;
    }
    qDebug() << "Connected";
    
    // Send HTTP GET request
    QString request = QString("GET %1 HTTP/1.1\r\n"
                             "Host: %2\r\n"
                             "Connection: close\r\n"
                             "\r\n").arg(path, host);
    
    qDebug() << "Sending HTTP request";
    send(sock, request.toUtf8().constData(), request.length(), 0);
    
    // Read response
    char buffer[65536];  // Larger buffer for faster download
    QByteArray response;
    int bytesRead;
    int totalBytes = 0;
    
    qDebug() << "Reading response...";
    auto lastLog = std::chrono::steady_clock::now();
    
    while ((bytesRead = recv(sock, buffer, sizeof(buffer), 0)) > 0)
    {
        response.append(buffer, bytesRead);
        totalBytes += bytesRead;
        
        // Log progress every 5 seconds
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastLog).count();
        if (elapsed >= 5)
        {
            qDebug() << "Downloaded" << totalBytes << "bytes so far...";
            lastLog = now;
        }
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

// Also add to poll() to monitor WebSocket health:
void OriginBackendSimple::poll()
{
    if (!m_connected || !m_webSocket)
        return;
    
    static auto lastPollTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPollTime).count();
    
    // Log if poll() wasn't called for a long time (indicates blocking)
    if (elapsed > 5000)  // More than 5 seconds
    {
        qDebug() << "WARNING: poll() was blocked for" << elapsed << "ms - WebSocket may timeout!";
    }
    lastPollTime = now;
    
    // Check WebSocket connection status
    if (!m_webSocket->isConnected())
    {
        qDebug() << "ERROR: WebSocket disconnected!";
        m_connected = false;
        return;
    }
    
    // Check for incoming messages
    int messageCount = 0;
    while (m_webSocket->hasData())
    {
        std::string message = m_webSocket->receiveText();
        if (!message.empty())
        {
            messageCount++;
            qDebug() << "RECEIVED MESSAGE:" << QString::fromStdString(message);
            processMessage(message);
        }
    }
    
    if (messageCount > 0)
    {
        qDebug() << "Processed" << messageCount << "messages";
    }
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
