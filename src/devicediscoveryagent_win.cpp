#include "devicediscoveryagent_win.h"

#ifdef Q_OS_WIN

#include "device.h"
#include "devicemanager.h"

#include <QDebug>
#include <QGuiApplication>
#include <QUuid>
#include <QWindow>

extern "C" {
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <usbioctl.h>
#include <dbt.h>
#include <usb100.h>
#include <usbiodef.h>
#include <winusb.h>
}

DeviceDiscoveryAgentPrivate::DeviceDiscoveryAgentPrivate() {
    QCoreApplication::instance()->installNativeEventFilter(this);

    HWND wnd = (HWND) QGuiApplication::topLevelWindows().at(0)->winId();

    HidD_GetHidGuid(&hid_class_guid);
    searchDevices();

    DEV_BROADCAST_DEVICEINTERFACE notification_filter;
    ZeroMemory(&notification_filter, sizeof(notification_filter));
    notification_filter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notification_filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    m_dev_notify = RegisterDeviceNotification(wnd, &notification_filter, DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    Q_ASSERT(m_dev_notify);
}

bool DeviceDiscoveryAgentPrivate::nativeEventFilter(const QByteArray& eventType, void* message, long* /* result */)
{
    if (eventType != "windows_generic_MSG") return false;

    MSG* msg = static_cast<MSG*>(message);
    if (msg->message != WM_DEVICECHANGE) return false;

    if (msg->wParam == DBT_DEVICEARRIVAL) {
        PDEV_BROADCAST_HDR hdr = (PDEV_BROADCAST_HDR) msg->lParam;
        if (hdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) return false;
        PDEV_BROADCAST_DEVICEINTERFACE notification = (PDEV_BROADCAST_DEVICEINTERFACE) msg->lParam;
        if (QUuid(hid_class_guid) != QUuid(notification->dbcc_classguid)) return false;
        auto id = QString::fromWCharArray(notification->dbcc_name).toLower();
        if (filter(id)) return false;
        qDebug() << "DBT_DEVICEARRIVAL ID=" << id;
        addDevice(id);
    } else if (msg->wParam == DBT_DEVICEREMOVECOMPLETE) {
        PDEV_BROADCAST_HDR hdr = (PDEV_BROADCAST_HDR) msg->lParam;
        if (hdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) return false;
        PDEV_BROADCAST_DEVICEINTERFACE notification = (PDEV_BROADCAST_DEVICEINTERFACE) msg->lParam;
        auto id = QString::fromWCharArray(notification->dbcc_name).toLower();
        auto device = m_devices.take(id);
        if (!device) return false;
        qDebug() << "DBT_DEVICEREMOVECOMPLETE ID=" << id;
        DeviceManager::instance()->removeDevice(device->q);
        device->q->deleteLater();
    }
    return false;
}

bool DeviceDiscoveryAgentPrivate::filter(const QString& id)
{
    Q_ASSERT(id == id.toLower());
    QRegExp re("vid_(\\w{4})&pid_(\\w{4})");
    return re.indexIn(id.toLower(), 0) == -1;
}

void DeviceDiscoveryAgentPrivate::searchDevices()
{
    SP_DEVINFO_DATA device_info_data;
    ZeroMemory(&device_info_data, sizeof(SP_DEVINFO_DATA));
    device_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

    SP_DEVICE_INTERFACE_DATA device_interface_data;
    ZeroMemory(&device_interface_data, sizeof(SP_DEVICE_INTERFACE_DATA));
    device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    HDEVINFO device_info_set = SetupDiGetClassDevs(&hid_class_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    for (DWORD device_index = 0; SetupDiEnumDeviceInfo(device_info_set, device_index, &device_info_data); device_index++) {
        DWORD required_length;
        SetupDiGetDeviceRegistryProperty(device_info_set, &device_info_data, SPDRP_HARDWAREID, NULL, NULL, 0, &required_length);
        wchar_t* hardware_id_data = new wchar_t[required_length];
        SetupDiGetDeviceRegistryProperty(device_info_set, &device_info_data, SPDRP_HARDWAREID, NULL, (PBYTE)hardware_id_data, required_length, NULL);
        const QString hardware_id = QString::fromWCharArray(hardware_id_data).toLower();
        delete hardware_id_data;
        if (filter(hardware_id)) continue;
        for (DWORD member_index = 0; SetupDiEnumDeviceInterfaces(device_info_set, &device_info_data, &hid_class_guid, member_index, &device_interface_data); member_index++) {
            SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, NULL, 0, &required_length, NULL);
            PSP_DEVICE_INTERFACE_DETAIL_DATA device_interface_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA) calloc(1, required_length);
            device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            SetupDiGetDeviceInterfaceDetail(device_info_set, &device_interface_data, device_interface_detail_data, required_length, NULL, NULL);
            auto id = QString::fromWCharArray(device_interface_detail_data->DevicePath).toLower();
            free(device_interface_detail_data);
            addDevice(id);
        }
    }

    if (device_info_set) SetupDiDestroyDeviceInfoList(device_info_set);
}

void DeviceDiscoveryAgentPrivate::addDevice(const QString& id)
{
    wchar_t path[id.size()+1];
    ZeroMemory(path, sizeof(wchar_t) * id.size() + 1);
    id.toWCharArray(path);
    HANDLE handle = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            wchar_t buf[1024];
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                buf, (sizeof(buf) / sizeof(wchar_t)), NULL);
            qDebug() << id << error << QString::fromWCharArray(buf);
        }
        return;
    }
    addDevice(id, handle);
    //CloseHandle(handle);
}

void DeviceDiscoveryAgentPrivate::addDevice(const QString& id, HANDLE handle)
{
    HIDD_ATTRIBUTES attrib;
    attrib.Size = sizeof(HIDD_ATTRIBUTES);
    if (!HidD_GetAttributes(handle, &attrib)) return;
    if (attrib.VendorID != 0x2C97 && (attrib.ProductID != 0x0001 || attrib.ProductID != 0x0004)) return;

    PHIDP_PREPARSED_DATA preparsed_data;
    if (!HidD_GetPreparsedData(handle, &preparsed_data)) return;
    HIDP_CAPS capabilities;
    NTSTATUS status = HidP_GetCaps(preparsed_data, &capabilities);
    HidD_FreePreparsedData(preparsed_data);
    if (status != HIDP_STATUS_SUCCESS) return;
    if (capabilities.UsagePage != 0xFFA0) return;

    qDebug() << "OutputReportByteLength=" << capabilities.OutputReportByteLength;
    qDebug() << "InputReportByteLength=" << capabilities.InputReportByteLength;

    DevicePrivateImpl* impl = new DevicePrivateImpl;
    impl->id = id;
    impl->handle = handle;
    memset(&impl->ol, 0, sizeof(impl->ol));
    impl->ol.hEvent = CreateEvent(NULL, FALSE, FALSE /*initial state f=nonsignaled*/, NULL);

    m_devices.insert(id, impl);
    //device->setObjectName(id);
    auto d = new Device(impl);

    auto t = new QTimer(d);
    QObject::connect(t, &QTimer::timeout, [impl, t] {
        if (!impl->reading) {
            impl->reading = true;
            memset(impl->buf, 0, 65);
            ResetEvent(impl->ol.hEvent);
            DWORD bytes_read, bytes_read_1;
            qDebug("reading");
            auto r = ReadFile(impl->handle, impl->buf, 65, &bytes_read_1, &impl->ol);
            if (!r && GetLastError() != ERROR_IO_PENDING) {
                CancelIo(impl->handle);
                impl->reading = false;
                qDebug("failed reading");
                return;
            }
        }

        qDebug("before wait for single object");
        auto res = WaitForSingleObject(impl->ol.hEvent, 0);
    		if (res != WAIT_OBJECT_0) {
            qDebug("nothing... see you soon");
            return;
        }

        impl->reading = false;
        DWORD bytes_read;
        qDebug("before getoverlapped result");
        GetOverlappedResult(impl->handle, &impl->ol, &bytes_read, TRUE/*wait*/);

        qDebug() << "read!";
        Q_ASSERT(bytes_read == 65);
        impl->inputReport(QByteArray::fromRawData(impl->buf + 1, 64));
    });

    QTimer::singleShot(200, d, [this, d, id, t] {
        //manager->exchange(handle, new GetFirmwareCommand);
        auto cmd = new GetAppNameCommand;
        QObject::connect(cmd, &Command::finished, [this, d, id] {
            if (d->appName().startsWith("OLOS")) {
                m_devices.remove(id);
                d->deleteLater();
            } else {
                DeviceManager::instance()->addDevice(d);
            }
        });
        d->exchange(cmd);
        t->start(10);
    });
}

QList<QByteArray> transport(const QByteArray& data) {
    QList<QByteArray> packets;
    int offset = 0;
    //qDebug() << "wrapping" << data.toHex();
    while (offset < data.size()) {
        QByteArray result;
        auto d = data.mid(offset, 64 - (packets.empty() ? 7 : 5));
        offset += d.size();
        QDataStream stream(&result, QIODevice::WriteOnly);
        stream << uint16_t(0x0101) << uint8_t(0x05) << uint16_t(packets.size());
        if (packets.empty()) stream << uint16_t(data.length());
        result.append(d);
        //qDebug() << "  packet " << result.toHex() << result.size();
        Q_ASSERT(result.length() <= 64);
        result = result.leftJustified(64, 0x0);
        packets.append(result);
    }
    return packets;
}

void _write(HANDLE handle, const QByteArray& report)
{
  DWORD bytes_written;
	BOOL res;
	OVERLAPPED ol;
	memset(&ol, 0, sizeof(ol));
  qDebug() << "   packet:" << report.toHex();
  WriteFile(handle, report.constData(), report.size(), NULL, &ol);
  GetOverlappedResult(handle, &ol, &bytes_written, TRUE/*wait*/);
  qDebug() << "    - packet:" << report.toHex() << bytes_written;
}
void DevicePrivateImpl::exchange(Command* command)
{
    qDebug() << "EXCHANGE" << queue.empty();
    const bool send = queue.empty();
    if (send) {
        const auto payload = command->payload();
        qDebug() << "send " << payload.toHex();
        for (const auto& packet : transport(payload)) {
            QByteArray report;
            report.append(uint8_t(0));
            report.append(packet);
            _write(handle, report);
            //int res = write(fd, report.constData(), report.size());
            //Q_ASSERT(res == report.size());
        }
        //qDebug() << "send done";
    }
    queue.enqueue(command);
    if (send) q->busyChanged();
}

void DevicePrivateImpl::inputReport(const QByteArray& data)
{
    //qDebug() << "read hid" << data.toHex();
    if (queue.empty()) {
        qDebug() << "READ UNKNOWN REPORT" << data.toHex();
        return;
    }
    Q_ASSERT(!queue.empty());
    QDataStream stream(data);
    auto command = queue.head();
    int r = command->readHIDReport(q, stream);
    if (r == 2) return;
    if (r == 1) qWarning("command failed");
    queue.dequeue();
    qDebug() << "input report done, queue size = " << queue.size();
    if (!queue.empty()) {
        //qDebug() << "sending next command";
        command = queue.head();
        const auto payload = command->payload();
        //qDebug() << "send " << payload.toHex();
        for (const auto& packet : transport(payload)) {
            qDebug() << "send packet " << packet.toHex();
            //auto res = IOHIDDeviceSetReport(handle, kIOHIDReportTypeOutput, 0, (const uint8_t*) packet.constData(), packet.size());
            QByteArray report;
            report.append(uint8_t(0));
            report.append(packet);
            _write(handle, report);
        }
        //qDebug() << "send done";
    } else {
        q->busyChanged();
    }
}

#endif // Q_OS_WIN
