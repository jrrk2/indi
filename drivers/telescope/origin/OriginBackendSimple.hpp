#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QDateTime>
#include "SimpleWebSocket.h"
#include "TelescopeDataProcessor.hpp"

class OriginBackendSimple : public QObject
{
    Q_OBJECT

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

    explicit OriginBackendSimple(QObject *parent = nullptr);
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
    
    // Polling - call this from INDI TimerHit()
    void poll();

signals:
    void tiffImageDownloaded(const QString& path, const QByteArray& data,
                            double ra, double dec, double exposure);
    void statusUpdated();

private:
    SimpleWebSocket *m_webSocket;
    TelescopeDataProcessor *m_dataProcessor;
    QNetworkAccessManager *m_networkManager;
    
    QString m_connectedHost;
    int m_connectedPort;
    bool m_connected;
    bool m_logicallyConnected;
    bool m_cameraConnected;
    
    TelescopeStatus m_status;
    int m_nextSequenceId;
    
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
