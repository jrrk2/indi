#include "indi_origin.h"
#include "OriginBackendSimple.hpp"
#include <indicom.h>
#include <memory>
#include <cstring>
#include <cmath>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>

#warning "compiling indi_origin.cpp"

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
    
    qDebug() << ("initProperties() called");
    
    SetTelescopeCapability(
        TELESCOPE_CAN_GOTO | 
        TELESCOPE_CAN_SYNC | 
        TELESCOPE_CAN_ABORT |
        TELESCOPE_CAN_PARK |
        TELESCOPE_HAS_TIME |
        TELESCOPE_HAS_LOCATION,
        4
    );
    
    // Connection address
    IUFillText(&AddressT[0], "HOST", "Host", "192.168.1.169");
    IUFillText(&AddressT[1], "PORT", "Port", "80");
    IUFillTextVector(&AddressTP, AddressT, 2, getDeviceName(),
                     "DEVICE_ADDRESS", "Server", CONNECTION_TAB, IP_RW, 60, IPS_IDLE);
    
    addDebugControl();
    
    qDebug() << ("initProperties() complete");
    
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
    qDebug() << ("=== Connect() START ===");
    
    // Create backend
    m_backend = new OriginBackendSimple();
    
    // Get connection settings
    QString host = QString::fromUtf8(AddressT[0].text);
    int port = atoi(AddressT[1].text);
    
    qDebug() << "Connecting to " << host.toUtf8().constData() << ":" << port;
    
    if (!m_backend->connectToTelescope(host, port))
    {
        qDebug() << ("Failed to connect to Origin Telescope");
        delete m_backend;
        m_backend = nullptr;
        return false;
    }
    
    m_backend->setConnected(true);
    m_connected = true;

    // Start the INDI base class timer - it will automatically call ReadScopeStatus()
    SetTimer(getCurrentPollingPeriod());
    qDebug() << ("Timer set");
    
    // Create camera device
    if (!camera)
    {
        camera = std::make_unique<OriginCamera>(m_backend);
        camera->initProperties();
        camera->ISGetProperties(nullptr);
    }
    
    qDebug() << ("=== Connect() COMPLETE ===");
    return true;
}

bool OriginTelescope::Disconnect()
{
    qDebug() << ("Disconnecting from Origin Telescope");
    
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
    {
        qDebug() << ("ReadScopeStatus called but not connected");
        return false;
    }
    
    // Poll the backend to get latest data
    m_backend->poll();
    
    auto status = m_backend->status();
    
    qDebug() << "Backend status: RA=" <<
      status.raPosition << " Dec=" << status.decPosition << "Tracking=" << status.isTracking << "Slewing=" << status.isSlewing;
    
    // Update coordinates
    m_currentRA = status.raPosition;
    m_currentDec = status.decPosition;
    
    qDebug() << "Setting INDI coords: RA=" << m_currentRA << " Dec=" << m_currentDec;
    
    // Update the internal INDI state
    NewRaDec(m_currentRA, m_currentDec);
    
    // CRITICAL: Actually send the coordinates to the INDI client
    // This is what makes the coordinates appear in Ekos
    EqNP.apply();
    
    qDebug() << "After EqNP.apply() - coordinates sent to client";
    
    // Update tracking state
    if (status.isSlewing)
    {
        TrackState = SCOPE_SLEWING;
        qDebug() << ("State: SLEWING");
    }
    else if (status.isTracking)
    {
        TrackState = SCOPE_TRACKING;
        qDebug() << ("State: TRACKING");
    }
    else if (status.isParked)
    {
        TrackState = SCOPE_PARKED;
        qDebug() << ("State: PARKED");
    }
    else
    {
        TrackState = SCOPE_IDLE;
    }
    
    return true;
}

bool OriginTelescope::Goto(double ra, double dec)
{
    if (!m_backend || !m_connected)
        return false;
    
    qDebug() << "Slewing to RA:" << ra << " Dec:" << dec;
    
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
    
    qDebug() << "Syncing to RA:" << ra << " Dec:" << dec;
    
    return m_backend->syncPosition(ra, dec);
}

bool OriginTelescope::Abort()
{
    if (!m_backend || !m_connected)
        return false;
    
    qDebug() << ("Aborting slew");
    return m_backend->abortMotion();
}

bool OriginTelescope::Park()
{
    if (!m_backend || !m_connected)
        return false;
    
    qDebug() << ("Parking telescope");
    return m_backend->parkMount();
}

bool OriginTelescope::UnPark()
{
    if (!m_backend || !m_connected)
        return false;
    
    qDebug() << ("Unparking telescope");
    return m_backend->unparkMount();
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
    qDebug() << ("Origin Camera connected");
    return true;
}

bool OriginCamera::Disconnect()
{
    qDebug() << ("Origin Camera disconnected");
    return true;
}
//=============================================================================
// CAMERA IMPLEMENTATION - Using callback mechanism
//=============================================================================

// Add to OriginCamera constructor
OriginCamera::OriginCamera(OriginBackendSimple *backend)
    : m_backend(backend)
{
    setVersion(1, 0);
    
    // Set up the image callback to be notified when images are ready
    if (m_backend)
    {
        m_backend->setImageCallback([this](const ImageInfo& info) {
            this->onImageReady(info);
        });
    }
}

// This is called by the backend when an image notification arrives
void OriginCamera::onImageReady(const ImageInfo& info)
{
    qDebug() << "Image ready callback received:" << info.fileLocation;
    
    m_pendingImageInfo = info;
    m_imageReady = true;
    
    // If we're in exposure, the TimerHit will handle the download
    // If not in exposure, this might be a stray notification
}

bool OriginCamera::processAndUploadImage(const QByteArray& imageData)
{
    int width = 4144;
    int height = 2822;
    
    // Allocate buffer
    PrimaryCCD.setFrame(0, 0, width, height);
    PrimaryCCD.setFrameBufferSize(width * height * sizeof(uint16_t));
    
    uint16_t *image = (uint16_t *)PrimaryCCD.getFrameBuffer();
    
    // TODO: Parse the actual image format (FITS, JPEG, etc.)
    // For now, check if it's FITS or needs conversion
    
    if (imageData.startsWith("SIMPLE"))
    {
        // It's a FITS file - parse it
        qDebug() << "Image is FITS format";
        // TODO: Use a FITS library to parse
        // For now, just copy raw data (placeholder)
        memcpy(image, imageData.data(), std::min((size_t)imageData.size(), 
                                                  width * height * sizeof(uint16_t)));
    }
    else
    {
        // Might be JPEG or other format
        qDebug() << "Image appears to be non-FITS format, needs conversion";
        // TODO: Convert JPEG/PNG to 16-bit grayscale
        
        // For testing, create a test pattern
        for (int i = 0; i < width * height; i++)
        {
            image[i] = (i % 65536);
        }
    }
    
    // Set FITS header info
    PrimaryCCD.setExposureDuration(m_exposureDuration);
    
    // Add WCS info if available
    if (m_pendingImageInfo.ra > 0 && m_pendingImageInfo.dec > 0)
    {
        // TODO: Add RA/Dec to FITS header
        qDebug() << "Image coordinates: RA=" << m_pendingImageInfo.ra 
                 << "Dec=" << m_pendingImageInfo.dec;
    }
    
    // Send the image to the client
    ExposureComplete(&PrimaryCCD);
    
    return true;
}

bool OriginCamera::StartExposure(float duration)
{
    if (!m_backend)
        return false;
    
    qDebug() << "Starting exposure:" << duration << "seconds";
    
    // Actually tell the Origin telescope to take a snapshot!
    if (!m_backend->takeSnapshot(duration, 100))
    {
        qDebug() << "Failed to send takeSnapshot command to telescope";
        return false;
    }
    
    m_exposureDuration = duration;
    m_exposureStart = currentTime();  // Record when we started
    
    PrimaryCCD.setExposureDuration(duration);
    PrimaryCCD.setExposureLeft(duration);  // Set initial countdown
    InExposure = true;
    
    return true;
}

bool OriginCamera::AbortExposure()
{
    if (!m_backend)
        return false;
    
    qDebug() << "Aborting exposure";
    
    InExposure = false;
    
    // Tell the telescope to abort if there's an abort command
    // m_backend->abortExposure();  // Add this if you have it
    
    return true;
}

bool OriginCamera::UpdateCCDFrame(int, int, int, int)
{
    return true;
}

bool OriginCamera::UpdateCCDBin(int binx, int biny)
{
    if (!m_backend)
        return false;
    
    qDebug() << "Setting binning to" << binx << "x" << biny;
    
    // Tell the backend to set binning
    // m_backend->setBinning(binx, biny);  // Add this if you have it
    
    return true;
}

void OriginCamera::TimerHit()
{
    if (!isConnected())
        return;
    
    if (InExposure)
    {
        // Calculate elapsed time
        double elapsed = currentTime() - m_exposureStart;
        double remaining = m_exposureDuration - elapsed;
        
        if (remaining <= 0)
        {
            qDebug() << "Exposure complete, waiting for image download...";
            
            // Check if image is ready from backend
            auto data = m_backend->getData();
            if (!data.lastImage.fileLocation.isEmpty())
            {
                qDebug() << "Image available at:" << data.lastImage.fileLocation;
                
                // Download and process the image
                if (downloadImage())
                {
                    qDebug() << "Image downloaded and sent to client";
                }
                else
                {
                    qDebug() << "Failed to download image";
                    PrimaryCCD.setExposureFailed();
                }
                
                InExposure = false;
            }
            else
            {
                // Image not ready yet, keep waiting
                qDebug() << "Waiting for image from telescope...";
            }
        }
        else
        {
            // Update exposure countdown
            PrimaryCCD.setExposureLeft(remaining);
        }
    }
    
    SetTimer(getCurrentPollingPeriod());
}

// Helper function to get current time in seconds
double OriginCamera::currentTime()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// Download and process the image
bool OriginCamera::downloadImage()
{
    if (!m_backend)
        return false;
    
    auto data = m_backend->getData();
    
    if (data.lastImage.fileLocation.isEmpty())
    {
        qDebug() << "No image location available";
        return false;
    }
    
    qDebug() << "Downloading image from:" << data.lastImage.fileLocation;
    
    // TODO: Download the actual image file via HTTP
    // For now, just create a dummy image to test the flow
    
    int width = 4144;
    int height = 2822;
    
    // Allocate buffer
    PrimaryCCD.setFrame(0, 0, width, height);
    PrimaryCCD.setFrameBufferSize(width * height * sizeof(uint16_t));
    
    uint16_t *image = (uint16_t *)PrimaryCCD.getFrameBuffer();
    
    // TODO: Replace this with actual image download and conversion
    // For testing, fill with a test pattern
    for (int i = 0; i < width * height; i++)
    {
        image[i] = (i % 65536);  // Test pattern
    }
    
    // Set FITS header info
    PrimaryCCD.setExposureDuration(m_exposureDuration);
    
    // Send the image to the client
    ExposureComplete(&PrimaryCCD);
    
    return true;
}

void OriginCamera::handleNewImage(const QString& path, const QByteArray& data, 
                                  double ra, double dec, double exposure)
{
    qDebug() << "New image notification received:" << path;
    // This is called when the backend detects a new image notification
    // The actual download happens in TimerHit when exposure is complete
}
