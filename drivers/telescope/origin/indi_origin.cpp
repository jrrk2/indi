#include "indi_origin.h"
#include "OriginBackendSimple.hpp"
#include <indicom.h>
#include <memory>
#include <cstring>
#include <cmath>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <tiffio.h>
#include <cstring>
#include <QFile>

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
    IUFillText(&AddressT[0], "HOST", "Host", "192.168.1.195");
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
    
    if (false) qDebug() << "Backend status: RA=" <<
      status.raPosition << " Dec=" << status.decPosition << "Tracking=" << status.isTracking << "Slewing=" << status.isSlewing;
    
    // Update coordinates
    m_currentRA = status.raPosition;
    m_currentDec = status.decPosition;
    
    if (false) qDebug() << "Setting INDI coords: RA=" << m_currentRA << " Dec=" << m_currentDec;
    
    // Update the internal INDI state
    NewRaDec(m_currentRA, m_currentDec);
    
    // CRITICAL: Actually send the coordinates to the INDI client
    // This is what makes the coordinates appear in Ekos
    EqNP.apply();
    
    if (false) qDebug() << "After EqNP.apply() - coordinates sent to client";
    
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
// CAMERA IMPLEMENTATION - Using callback data directly
//=============================================================================

OriginCamera::OriginCamera(OriginBackendSimple *backend)
    : m_backend(backend)
{
    setVersion(1, 0);
    
    // Set up the image callback - backend already downloads the image!
    if (m_backend)
    {
        m_backend->setImageCallback([this](const QString& path, const QByteArray& data, 
                                            double ra, double dec, double exposure) {
            // Backend has already downloaded the image data for us!
            this->onImageReady(path, data, ra, dec);
        });
    }
}

OriginCamera::~OriginCamera()
{
    // Cleanup if needed
}

const char *OriginCamera::getDefaultName()
{
    return "Origin Camera";
}

bool OriginCamera::updateProperties()
{
    INDI::CCD::updateProperties();
    return true;
}

bool OriginCamera::Connect()
{
    qDebug() << ("Origin Camera connected");
    // START THE CAMERA'S TIMER!
    SetTimer(getCurrentPollingPeriod());
    return true;
}

bool OriginCamera::Disconnect()
{
    qDebug() << ("Origin Camera disconnected");
    return true;
}

// This is called by the backend when image is downloaded
void OriginCamera::onImageReady(const QString& filePath, const QByteArray& imageData, 
                                 double ra, double dec)
{
    qDebug() << "Image ready callback received:" << filePath 
             << "Size:" << imageData.size() << "bytes";
    
    m_pendingImagePath = filePath;
    m_pendingImageData = imageData;
    m_pendingImageRA = ra;
    m_pendingImageDec = dec;
    m_imageReady = true;
    
    // If we're in exposure, the TimerHit will handle processing and uploading
}

bool OriginCamera::StartExposure(float duration)
{
    if (!m_backend)
        return false;
    
    qDebug() << "Starting exposure:" << duration << "seconds";
    
    // Clear any previous image ready flag
    m_imageReady = false;
    m_pendingImagePath.clear();
    m_pendingImageData.clear();
    
    // Tell the Origin telescope to take a snapshot!
    if (!m_backend->takeSnapshot(duration, 100))
    {
        qDebug() << "Failed to send takeSnapshot command to telescope";
        return false;
    }
    
    m_exposureDuration = duration;
    m_exposureStart = currentTime();
    
    PrimaryCCD.setExposureDuration(duration);
    PrimaryCCD.setExposureLeft(duration);
    InExposure = true;
    
    return true;
}

bool OriginCamera::AbortExposure()
{
    if (!m_backend)
        return false;
    
    qDebug() << "Aborting exposure";
    
    InExposure = false;
    m_imageReady = false;
    m_pendingImageData.clear();
    
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
            // Exposure time has elapsed
            if (m_imageReady && !m_pendingImageData.isEmpty())
            {
                // Image data is ready to process!
                qDebug() << "Exposure complete and image data ready, processing...";
                
                if (processAndUploadImage(m_pendingImageData))
                {
                    qDebug() << "Image processed and sent to client";
                    InExposure = false;
                    m_imageReady = false;
                    m_pendingImageData.clear();
                }
                else
                {
                    qDebug() << "Failed to process image";
                    PrimaryCCD.setExposureFailed();
                    InExposure = false;
                    m_imageReady = false;
                    m_pendingImageData.clear();
                }
            }
            else
            {
                // Exposure time done but image not ready yet, keep waiting
	      if (false) qDebug() << "Exposure time complete, waiting for image from telescope...";
                PrimaryCCD.setExposureLeft(0);
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

bool OriginCamera::initProperties()
{
    INDI::CCD::initProperties();
    
    SetCCDCapability(CCD_CAN_ABORT);
    
    // Origin camera dimensions in snapshot mode
    SetCCDParams(3056, 2048, 16, 3.76, 3.76);
    
    // CRITICAL: Set the exposure range with 1 microsecond resolution
    // Min: 1 microsecond (0.000001s)
    // Max: 1 hour (3600s)
    // Step: 1 microsecond (0.000001s)
    PrimaryCCD.setMinMaxStep("CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", 0.000001, 3600, 0.000001, false);
    
    addDebugControl();
    
    return true;
}

bool OriginCamera::processAndUploadImage(const QByteArray& imageData)
{
    qDebug() << "Processing 16-bit RGB TIFF with libtiff:" << imageData.size() << "bytes";
    
    // Write TIFF data to a temporary file for libtiff to read
    // (libtiff can also read from memory but file is simpler)
    QString tempPath = QString("/tmp/origin_temp_%1.tiff").arg(QDateTime::currentMSecsSinceEpoch());
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly))
    {
        qDebug() << "Failed to create temp file";
        return false;
    }
    tempFile.write(imageData);
    tempFile.close();
    
    // Open TIFF with libtiff
    TIFF* tif = TIFFOpen(tempPath.toUtf8().constData(), "r");
    if (!tif)
    {
        qDebug() << "Failed to open TIFF with libtiff";
        QFile::remove(tempPath);
        return false;
    }
    
    // Read TIFF properties
    uint32_t width, height;
    uint16_t samplesperpixel, bitspersample, photometric, config;
    
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesperpixel);
    TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitspersample);
    TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
    TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &config);
    
    qDebug() << "TIFF properties: width=" << width << "height=" << height
             << "samples=" << samplesperpixel << "bits=" << bitspersample
             << "photometric=" << photometric << "planar=" << config;
    
    if (samplesperpixel != 3 || bitspersample != 16)
    {
        qDebug() << "Unexpected TIFF format";
        TIFFClose(tif);
        QFile::remove(tempPath);
        return false;
    }
    
    // Set up for 3-axis FITS (RGB cube)
    PrimaryCCD.setFrame(0, 0, width, height);
    PrimaryCCD.setExposureDuration(m_exposureDuration);
    PrimaryCCD.setNAxis(3);
    
    // Allocate buffer
    int planeSize = width * height;
    PrimaryCCD.setFrameBufferSize(planeSize * 3 * sizeof(uint16_t));
    uint16_t *image = (uint16_t *)PrimaryCCD.getFrameBuffer();
    
    // Allocate scanline buffer
    uint16_t *scanline = (uint16_t *)_TIFFmalloc(TIFFScanlineSize(tif));
    
    if (!scanline)
    {
        qDebug() << "Failed to allocate scanline buffer";
        TIFFClose(tif);
        QFile::remove(tempPath);
        return false;
    }
    
    // Read TIFF data line by line and convert to planar RGB
    qDebug() << "Reading TIFF data...";
    
    for (uint32_t row = 0; row < height; row++)
    {
        if (TIFFReadScanline(tif, scanline, row) < 0)
        {
            qDebug() << "Error reading scanline" << row;
            break;
        }
        
        // Convert interleaved RGB to planar RGB
        // scanline format: R0 G0 B0 R1 G1 B1 R2 G2 B2 ...
        // output format: R0 R1 R2 ... G0 G1 G2 ... B0 B1 B2 ...
        for (uint32_t col = 0; col < width; col++)
        {
            int idx = row * width + col;
            int scanIdx = col * 3;
            
            image[idx] = scanline[scanIdx];                    // R plane
            image[planeSize + idx] = scanline[scanIdx + 1];    // G plane
            image[planeSize * 2 + idx] = scanline[scanIdx + 2]; // B plane
        }
    }
    
    // Cleanup
    _TIFFfree(scanline);
    TIFFClose(tif);
    //    QFile::remove(tempPath);
    qDebug() << "TIFF file left in: " << tempPath;
    
    // Sample center pixel to verify
    int centerIdx = (height / 2) * width + (width / 2);
    qDebug() << "Center pixel values: R=" << image[centerIdx] 
             << "G=" << image[planeSize + centerIdx]
             << "B=" << image[planeSize*2 + centerIdx];
    
    qDebug() << "3-axis RGB FITS ready, sending to Ekos";
    
    // Send to Ekos
    ExposureComplete(&PrimaryCCD);
    
    return true;
}

void OriginCamera::handleNewImage(const QString& path, const QByteArray& data, 
                                  double ra, double dec, double exposure)
{
    // This method is not used - we use the callback in constructor instead
    Q_UNUSED(path);
    Q_UNUSED(data);
    Q_UNUSED(ra);
    Q_UNUSED(dec);
    Q_UNUSED(exposure);
}
