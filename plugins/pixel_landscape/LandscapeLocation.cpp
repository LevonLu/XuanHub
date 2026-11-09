// LandscapeLocation.cpp — 前台授权与非阻塞 WinRT 定位，不留下 DLL 异步回调
#include "LandscapeLocation.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QTimeZone>
#include <windows.h>
#include <roapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <chrono>

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices::Geolocation;
using namespace winrt::Windows::Security::Authorization::AppCapabilityAccess;

namespace {
QString locationError(HRESULT code) {
    if (code == E_ACCESSDENIED) return QStringLiteral("denied");
    if (code == HRESULT_FROM_WIN32(ERROR_TIMEOUT) || code == HRESULT_FROM_WIN32(WAIT_TIMEOUT))
        return QStringLiteral("timeout");
    return QStringLiteral("unavailable");
}
}

struct WindowsLandscapeLocation::Impl {
    WindowsLandscapeLocation* owner;
    QTimer timer;
    QElapsedTimer elapsed;
    Geolocator locator{nullptr};
    IAsyncOperation<GeolocationAccessStatus> authorization{nullptr};
    IAsyncOperation<Geoposition> position{nullptr};
    quint64 id = 0;
    bool initialized = false;
    bool initAttempted = false;
    bool usable = false;

    explicit Impl(WindowsLandscapeLocation* value) : owner(value) {
        timer.setInterval(100);
        QObject::connect(&timer, &QTimer::timeout, owner, [this] { poll(); });
    }
    bool initialize() {
        if (!initAttempted) {
            initAttempted = true;
            const HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
            initialized = SUCCEEDED(hr);
            usable = initialized || hr == RPC_E_CHANGED_MODE;
        }
        return usable;
    }
    void clear() {
        timer.stop();
        // 无 Completed/StatusChanged 委托；取消后不会有 WinRT 线程回调已销毁的 QObject。
        try { if (authorization) authorization.Cancel(); } catch (...) {}
        try { if (position) position.Cancel(); } catch (...) {}
        authorization = nullptr;
        position = nullptr;
        locator = nullptr;
    }
    void fail(const QString& error) {
        const quint64 request = id;
        clear();
        Q_EMIT owner->finished(request, error, 0, 0, {});
    }
    void acquire() {
        locator = Geolocator();
        locator.DesiredAccuracy(PositionAccuracy::Default);
        position = locator.GetGeopositionAsync(std::chrono::minutes(5), std::chrono::seconds(15));
        elapsed.restart();
    }
    void poll() {
        try {
            if (elapsed.elapsed() > (authorization ? 60000 : 20000)) {
                fail(QStringLiteral("timeout")); return;
            }
            if (authorization) {
                if (authorization.Status() == AsyncStatus::Started) return;
                if (authorization.GetResults() != GeolocationAccessStatus::Allowed) {
                    fail(QStringLiteral("denied")); return;
                }
                authorization = nullptr;
                acquire();
            }
            if (!position || position.Status() == AsyncStatus::Started) return;
            const auto coordinate = position.GetResults().Coordinate();
            const auto point = coordinate.Point().Position();
            const auto timestamp = QDateTime::fromMSecsSinceEpoch(
                coordinate.Timestamp().time_since_epoch().count()/10000 - 11644473600000LL, QTimeZone::UTC);
            const quint64 request = id;
            clear();
            Q_EMIT owner->finished(request, {}, point.Latitude, point.Longitude, timestamp);
        } catch (const winrt::hresult_error& e) {
            fail(locationError(e.code()));
        } catch (...) { fail(QStringLiteral("unavailable")); }
    }
};

WindowsLandscapeLocation::WindowsLandscapeLocation(QObject* parent)
    : LandscapeLocation(parent), m_impl(std::make_unique<Impl>(this)) {}
WindowsLandscapeLocation::~WindowsLandscapeLocation() {
    cancel();
    if (m_impl->initialized) RoUninitialize();
}
QString WindowsLandscapeLocation::access() {
    try {
        if (!m_impl->initialize()) return QStringLiteral("unavailable");
        const auto status = AppCapability::Create(L"location").CheckAccess();
        if (status == AppCapabilityAccessStatus::Allowed) {
            const auto locator = Geolocator();
            return locator.LocationStatus() == PositionStatus::Disabled ? QStringLiteral("denied") : QStringLiteral("allowed");
        }
        if (status == AppCapabilityAccessStatus::DeniedBySystem || status == AppCapabilityAccessStatus::DeniedByUser)
            return QStringLiteral("denied");
        return QStringLiteral("prompt");
    } catch (...) { return QStringLiteral("unavailable"); }
}
void WindowsLandscapeLocation::request(quint64 id, bool interactive) {
    cancel();
    m_impl->id = id;
    try {
        if (!m_impl->initialize()) { m_impl->fail(QStringLiteral("unavailable")); return; }
        if (interactive) {
            DWORD foregroundProcess = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
            if (foregroundProcess != GetCurrentProcessId()) {
                m_impl->fail(QStringLiteral("foreground_required")); return;
            }
            m_impl->authorization = Geolocator::RequestAccessAsync();
        } else {
            const QString permission = access();
            if (permission != QStringLiteral("allowed")) { m_impl->fail(permission); return; }
            m_impl->acquire();
        }
        m_impl->elapsed.start();
        m_impl->timer.start();
    } catch (const winrt::hresult_error& e) {
        m_impl->fail(locationError(e.code()));
    } catch (...) { m_impl->fail(QStringLiteral("unavailable")); }
}
void WindowsLandscapeLocation::cancel() { m_impl->clear(); }
