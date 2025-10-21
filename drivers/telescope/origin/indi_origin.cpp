#include "indi_origin.h"
#include "OriginBackendSimple.hpp"
#include <indicom.h>
#include <memory>
#include <cstring>
#include <cmath>
#include <sys/time.h>
#include <unistd.h>

// INDI requires these for driver registration
std::unique_ptr<OriginTelescope> telescope(new OriginTelescope());
std::unique_ptr<OriginCamera> camera(nullptr);

//=============================================================================
// TELESCOPE IMPLEMENTATION
//=============================================================================

OriginTelescope::OriginTelescope()
{
    setVersion(1, 0);
    
    SetTelescopeCapability(
        TELESCOPE_CAN_GOTO | 
        TELESCOPE_CAN_SYNC | 
        TELESCOPE_CAN_ABORT |
        TELESCOPE_CAN_PARK |
        TELESCOPE_HAS_TIME |
        TELESCOPE_HAS_LOCATION,
        4
    );
}

OriginTelescope::~OriginTelescope()
{
    if (m_backend)
    {
        delete m_backend;
        m_backend = nullptr;
    }
}

const char *OriginTelescope::getDefaultName()
{
    return "Origin Telescope";
}

bool OriginTelescope::initProperties()
{
    INDI::Telescope::initProperties();
    
    // Connection address
    IUFillText(&AddressT[0], "HOST", "Host", "192.168.1.169");
    IUFillText(&AddressT[1], "PORT", "Port", "80");
    IUFillTextVector(&AddressTP, AddressT, 2, getDeviceName(),
                     "DEVICE_ADDRESS", "Server", CONNECTION_TAB, IP_RW, 60, IPS_IDLE);
    
    addDebugControl();
    
    return true;
}

bool OriginTelescope::updateProperties()
{
    INDI::Telescope::updateProperties();
    
    if (isConnected())
    {
        defineProperty(&AddressTP);
    }
    else
    {
        deleteProperty(AddressTP.name);
    }
    
    return true;
}

bool OriginTelescope::Connect()
{
    LOG_INFO("Connecting to Origin Telescope...");
    
    // Create backend
    m_backend = new OriginBackendSimple();
    
    // Get connection settings
    QString host = QString::fromUtf8(AddressT[0].text);
    int port = atoi(AddressT[1].text);
    
    LOGF_INFO("Connecting to %s:%d", host.toUtf8().constData(), port);
    
    if (!m_backend->connectToTelescope(host, port))
    {
        LOG_ERROR("Failed to connect to Origin Telescope");
        delete m_backend;
        m_backend = nullptr;
        return false;
    }
    
    m_backend->setConnected(true);
    m_connected = true;
    
    // Start INDI polling timer
    SetTimer(getCurrentPollingPeriod());
    
    // Create camera device
    if (!camera)
    {
        camera = std::make_unique<OriginCamera>(m_backend);
        camera->initProperties();
        camera->ISGetProperties(nullptr);
    }
    
    LOG_INFO("Connected to Origin Telescope");
    return true;
}

bool OriginTelescope::Disconnect()
{
    LOG_INFO("Disconnecting from Origin Telescope");
    
    if (m_backend)
    {
        m_backend->disconnectFromTelescope();
        delete m_backend;
        m_backend = nullptr;
    }
    
    m_connected = false;
    
    return true;
}

bool OriginTelescope::ReadScopeStatus()
{
    if (!m_backend || !m_connected)
        return false;
    
    auto status = m_backend->status();
    
    // Update coordinates
    m_currentRA = status.raPosition;
    m_currentDec = status.decPosition;
    
    NewRaDec(m_currentRA, m_currentDec);
    
    // Update state
    if (status.isSlewing)
        TrackState = SCOPE_SLEWING;
    else if (status.isTracking)
        TrackState = SCOPE_TRACKING;
    else if (status.isParked)
        TrackState = SCOPE_PARKED;
    else
        TrackState = SCOPE_IDLE;
    
    return true;
}

bool OriginTelescope::Goto(double ra, double dec)
{
    if (!m_backend || !m_connected)
        return false;
    
    LOGF_INFO("Slewing to RA: %.6f Dec: %.6f", ra, dec);
    
    if (m_backend->gotoPosition(ra, dec))
    {
        TrackState = SCOPE_SLEWING;
        return true;
    }
    
    return false;
}

bool OriginTelescope::Sync(double ra, double dec)
{
    if (!m_backend || !m_connected)
        return false;
    
    LOGF_INFO("Syncing to RA: %.6f Dec: %.6f", ra, dec);
    
    return m_backend->syncPosition(ra, dec);
}

bool OriginTelescope::Abort()
{
    if (!m_backend || !m_connected)
        return false;
    
    LOG_INFO("Aborting slew");
    return m_backend->abortMotion();
}

bool OriginTelescope::Park()
{
    if (!m_backend || !m_connected)
        return false;
    
    LOG_INFO("Parking telescope");
    return m_backend->parkMount();
}

bool OriginTelescope::UnPark()
{
    if (!m_backend || !m_connected)
        return false;
    
    LOG_INFO("Unparking telescope");
    return m_backend->unparkMount();
}

void OriginTelescope::TimerHit()
{
    if (!isConnected())
        return;
    
    // Poll the backend - this processes WebSocket messages
    if (m_backend)
    {
        m_backend->poll();
    }
    
    // Update scope status
    ReadScopeStatus();
    
    // Schedule next timer hit
    SetTimer(getCurrentPollingPeriod());
}

bool OriginTelescope::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        if (strcmp(name, AddressTP.name) == 0)
        {
            IUUpdateText(&AddressTP, texts, names, n);
            AddressTP.s = IPS_OK;
            IDSetText(&AddressTP, nullptr);
            return true;
        }
    }
    
    return INDI::Telescope::ISNewText(dev, name, texts, names, n);
}

//=============================================================================
// CAMERA IMPLEMENTATION
//=============================================================================

OriginCamera::OriginCamera(OriginBackendSimple *backend)
    : m_backend(backend)
{
    setVersion(1, 0);
}

const char *OriginCamera::getDefaultName()
{
    return "Origin Camera";
}

bool OriginCamera::initProperties()
{
    INDI::CCD::initProperties();
    
    SetCCDCapability(CCD_CAN_ABORT);
    
    // Origin camera specs
    SetCCDParams(4144, 2822, 16, 3.76, 3.76);
    
    addDebugControl();
    
    return true;
}

bool OriginCamera::updateProperties()
{
    INDI::CCD::updateProperties();
    return true;
}

bool OriginCamera::Connect()
{
    if (!m_backend || !m_backend->isConnected())
    {
        LOG_ERROR("Telescope must be connected first");
        return false;
    }
    
    m_backend->setCameraConnected(true);
    LOG_INFO("Origin Camera connected");
    
    // Set callback instead of connecting signal
    m_backend->setImageCallback([this](const QString& path, const QByteArray& data, 
                                       double ra, double dec, double exposure) {
        handleNewImage(path, data, ra, dec, exposure);
    });
    
    SetTimer(1000);
    
    return true;
}

bool OriginCamera::Disconnect()
{
    LOG_INFO("Origin Camera disconnected");
    return true;
}

bool OriginCamera::StartExposure(float duration)
{
    if (!m_backend || !m_backend->isConnected())
        return false;
    
    LOGF_INFO("Starting %.2f second exposure", duration);
    
    m_exposureDuration = duration;
    
    struct timeval now;
    gettimeofday(&now, nullptr);
    m_exposureStart = now.tv_sec + now.tv_usec / 1000000.0;
    
    if (m_backend->takeSnapshot(duration, 200))
    {
        PrimaryCCD.setExposureDuration(duration);
        return true;
    }
    
    return false;
}

bool OriginCamera::AbortExposure()
{
    if (PrimaryCCD.getExposureLeft() <= 0)
        return false;
    
    LOG_INFO("Aborting exposure");
    
    return m_backend->abortExposure();
}

bool OriginCamera::UpdateCCDFrame(int, int, int, int)
{
    return false;
}

bool OriginCamera::UpdateCCDBin(int binx, int biny)
{
    return (binx == 1 && biny == 1);
}

void OriginCamera::TimerHit()
{
    if (!isConnected())
        return;
    
    // Update exposure progress
    double remaining = PrimaryCCD.getExposureLeft();
    
    if (remaining > 0)
    {
        struct timeval now;
        gettimeofday(&now, nullptr);
        double currentTime = now.tv_sec + now.tv_usec / 1000000.0;
        
        double elapsed = currentTime - m_exposureStart;
        double newRemaining = m_exposureDuration - elapsed;
        
        if (newRemaining > 0)
        {
            PrimaryCCD.setExposureLeft(newRemaining);
        }
    }
    
    SetTimer(1000);
}

void OriginCamera::handleNewImage(const QString&, const QByteArray& data,
                                  double, double, double)
{
    if (PrimaryCCD.getExposureLeft() <= 0)
        return;
    
    LOG_INFO("Image received from Origin");
    
    // Set image buffer
    PrimaryCCD.setFrameBufferSize(data.size());
    memcpy(PrimaryCCD.getFrameBuffer(), data.constData(), data.size());
    
    // Set FITS headers
    PrimaryCCD.setImageExtension("tiff");
    
    // Notify completion
    ExposureComplete(&PrimaryCCD);
}
