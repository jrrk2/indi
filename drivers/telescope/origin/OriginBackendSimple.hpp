#pragma once

#include <QString>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <functional>
#include "SimpleWebSocket.h"
#include "TelescopeData.hpp"

class OriginBackendSimple
{
public:
    struct TelescopeStatus {
        double altPosition = 0.0;
        double azPosition = 0.0;
        double raPosition = 0.0;
        double decPosition = 0.0;
        bool isConnected = false;
        bool isLogicallyConnected = false;
        bool isCameraLogicallyConnected = false;
        bool isSlewing = false;
        bool isTracking = false;
        bool isParked = false;
        bool isAligned = false;
        QString currentOperation = "Idle";
        double temperature = 20.0;
    };

    // Callback types
    using ImageCallback = std::function<void(const QString&, const QByteArray&, double, double, double)>;
    using StatusCallback = std::function<void()>;

    explicit OriginBackendSimple();
    ~OriginBackendSimple();

    // Connection
    bool connectToTelescope(const QString& host, int port = 80);
    void disconnectFromTelescope();
    bool isConnected() const { return m_connected; }
    bool isLogicallyConnected() const { return m_logicallyConnected; }
    void setConnected(bool connected) { m_logicallyConnected = connected; }
    
    // Camera
    void setCameraConnected(bool connected) { m_cameraConnected = connected; }
    bool isCameraConnected() const { return m_cameraConnected; }
    
    // Mount operations
    bool gotoPosition(double ra, double dec);
    bool syncPosition(double ra, double dec);
    bool abortMotion();
    bool parkMount();
    bool unparkMount();
    bool setTracking(bool enabled);
    bool isTracking() const;
    
    // Camera operations
    bool takeSnapshot(double exposure, int iso);
    bool abortExposure();
    
    // Status
    TelescopeStatus status() const { return m_status; }
    double temperature() const { return m_status.temperature; }
    
    // Callbacks
    void setImageCallback(ImageCallback cb) { m_imageCallback = cb; }
    void setStatusCallback(StatusCallback cb) { m_statusCallback = cb; }
    
    // Polling - call this from INDI TimerHit()
    void poll();

private:
    SimpleWebSocket *m_webSocket;
    bool m_autoReconnect {true};
    QString m_lastConnectedHost;
    int m_lastConnectedPort {80};
    
    bool reconnectWebSocket();
    void setAutoReconnect(bool enable);
  
    QString m_connectedHost;
    int m_connectedPort;
    bool m_connected;
    bool m_logicallyConnected;
    bool m_cameraConnected;
    
    TelescopeStatus m_status;
    TelescopeData m_telescopeData;
    int m_nextSequenceId;
    
    // Callbacks
    ImageCallback m_imageCallback;
    StatusCallback m_statusCallback;
    QByteArray downloadImageSync(const QString& url);
    QString m_pendingImagePath;    
    // Message handling
    void processMessage(const std::string& message);
    void sendCommand(const QString& command, const QString& destination,
                    const QJsonObject& params = QJsonObject());
    void requestImage(const QString& filePath);
    
    // Coordinate conversion
    double hoursToRadians(double hours);
    double degreesToRadians(double degrees);
    double radiansToHours(double radians);
    double radiansToDegrees(double radians);
};
