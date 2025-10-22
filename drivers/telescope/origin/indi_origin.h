#pragma once

#include <inditelescope.h>
#include <indiccd.h>
#include <memory>

class OriginBackendSimple;  // Forward declaration

class OriginTelescope : public INDI::Telescope
{
public:
    OriginTelescope();
    virtual ~OriginTelescope();

    virtual const char *getDefaultName() override;
    virtual bool initProperties() override;
    virtual bool updateProperties() override;
    
    virtual bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;

protected:
    virtual bool Connect() override;
    virtual bool Disconnect() override;
    virtual bool ReadScopeStatus() override;
    virtual bool Goto(double ra, double dec) override;
    virtual bool Sync(double ra, double dec) override;
    virtual bool Abort() override;
    virtual bool Park() override;
    virtual bool UnPark() override;

private:
    ITextVectorProperty AddressTP;
    IText AddressT[2] {};
    
    OriginBackendSimple *m_backend {nullptr};
    
    double m_currentRA {0};
    double m_currentDec {0};
    bool m_connected {false};
    
    // REMOVED: INDI::Timer polling_timer;
    // We're using the base class timer mechanism instead
};

class OriginCamera : public INDI::CCD
{
public:
    OriginCamera(OriginBackendSimple *backend);
    virtual ~OriginCamera() = default;

    virtual const char *getDefaultName() override;
    virtual bool initProperties() override;
    virtual bool updateProperties() override;

protected:
    virtual bool Connect() override;
    virtual bool Disconnect() override;
    virtual bool StartExposure(float duration) override;
    virtual bool AbortExposure() override;
    virtual bool UpdateCCDFrame(int, int, int, int) override;
    virtual bool UpdateCCDBin(int binx, int biny) override;
    virtual void TimerHit() override;

private:
    OriginBackendSimple *m_backend;
    double m_exposureStart {0};
    double m_exposureDuration {0};
    
    void handleNewImage(const QString& path, const QByteArray& data, 
                       double ra, double dec, double exposure);
};
