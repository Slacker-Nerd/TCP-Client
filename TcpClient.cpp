#include "TcpClient.h"

static void tcpTaskEntry(void *param)
{
    ESP32TcpClient *self = static_cast<ESP32TcpClient *>(param);
    self->taskLoop();
}

ESP32TcpClient::ESP32TcpClient()
    : _ssid(nullptr), _pwd(nullptr), _pcPort(0), _recvCb(nullptr), _autoWifi(false)
{
    _serverIp[0] = '\0';
    _recvBuf[0] = '\0';
    _recvLen = 0;
}

ESP32TcpClient::~ESP32TcpClient()
{
    end();
}

void ESP32TcpClient::begin(const char *ssid, const char *pwd, const char *pcIp, uint16_t pcPort,
                           TcpRecvCallback cb, bool autoWifi)
{
    _ssid = ssid;
    _pwd = pwd;
    _recvCb = cb;
    _autoWifi = autoWifi;

    if (pcIp != nullptr && pcIp[0] != '\0')
    {
        setTarget(pcIp, pcPort);
    }
    else if (pcPort != 0 && _pcPort == 0)
    {
        _pcPort = pcPort;
    }

    // 任务已在运行：只更新参数，不重复创建
    if (_taskHandle != nullptr)
    {
        return;
    }

    _running = true;
    if (xTaskCreate(tcpTaskEntry, "tcp_task", 4096, this, 5, &_taskHandle) != pdPASS)
    {
        _taskHandle = nullptr;
        _running = false;
        Serial.println("[TCP] 后台任务创建失败");
    }
}

void ESP32TcpClient::setTarget(const char *pcIp, uint16_t pcPort)
{
    if (pcIp == nullptr)
    {
        return;
    }

    bool changed = (strncmp(_serverIp, pcIp, sizeof(_serverIp) - 1) != 0);

    strncpy(_serverIp, pcIp, sizeof(_serverIp) - 1);
    _serverIp[sizeof(_serverIp) - 1] = '\0';

    if (pcPort != 0 && pcPort != _pcPort)
    {
        _pcPort = pcPort;
        changed = true;
    }

    if (changed && _connected)
    {
        // 目标变了，断开旧连接，交给后台任务重连
        Serial.printf("[TCP] 目标切换为 %s:%u，断开旧连接\n", _serverIp, (unsigned)_pcPort);
        _client.stop();
        _connected = false;
        _recvLen = 0;
    }
}

void ESP32TcpClient::end()
{
    _running = false;
    _client.stop();
    _connected = false;
    _wifiReady = false;

    // 等后台任务自行退出，避免直接 vTaskDelete 打断正在进行的 socket 操作
    if (_taskHandle != nullptr)
    {
        for (int i = 0; i < 100 && _taskHandle != nullptr; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (_taskHandle != nullptr)
        {
            vTaskDelete(_taskHandle);
            _taskHandle = nullptr;
        }
    }
    _recvLen = 0;
}

void ESP32TcpClient::taskLoop()
{
    unsigned long lastTry = 0;
    unsigned long lastWifiTry = 0;
    const unsigned long RETRY_MS = 1000;

    for (;;)
    {
        /* ---------- 退出请求 ---------- */
        if (!_running)
        {
            _client.stop();
            _connected = false;
            _wifiReady = false;
            _taskHandle = nullptr;
            vTaskDelete(nullptr);
            return;
        }

        /* ---------- WiFi ---------- */
        if (WiFi.status() != WL_CONNECTED)
        {
            if (_wifiReady)
            {
                Serial.println("[TCP] WiFi 断开");
            }
            _wifiReady = false;
            if (_connected)
            {
                _client.stop();
                _connected = false;
                _recvLen = 0;
            }

            if (_autoWifi && _ssid != nullptr && millis() - lastWifiTry > 3000)
            {
                lastWifiTry = millis();
                WiFi.begin(_ssid, _pwd);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!_wifiReady)
        {
            _wifiReady = true;
            Serial.printf("[TCP] WiFi 就绪，本机 IP：%s\n", WiFi.localIP().toString().c_str());
        }

        /* ---------- 建链 ---------- */
        if (!_connected)
        {
            if (_serverIp[0] == '\0' || strcmp(_serverIp, "0.0.0.0") == 0 || _pcPort == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(300));
                continue;
            }

            if (millis() - lastTry < RETRY_MS)
            {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            lastTry = millis();

            Serial.printf("[TCP] 尝试连接 %s:%u ...\n", _serverIp, (unsigned)_pcPort);
            if (_client.connect(_serverIp, _pcPort, 3000))
            {
                _client.setNoDelay(true); // 关闭 Nagle，指令类小包低延迟
                _connected = true;
                _recvLen = 0;
                Serial.printf("[TCP] 连接成功：%s:%u\n", _serverIp, (unsigned)_pcPort);
            }
            else
            {
                _client.stop();
                Serial.printf("[TCP] 连接 %s:%u 失败，1s 后重试\n", _serverIp, (unsigned)_pcPort);
                vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
                lastTry = millis();
            }
            continue;
        }

        /* ---------- 收数据（非阻塞，按行分发） ---------- */
        bool got = false;
        while (_client.available() > 0)
        {
            int c = _client.read();
            if (c < 0)
            {
                break;
            }
            got = true;
            char ch = (char)c;

            if (ch == '\r')
            {
                continue; // 兼容 \r\n
            }
            if (ch == '\n')
            {
                if (_recvLen > 0)
                {
                    _recvBuf[_recvLen] = '\0';
                    if (_recvCb != nullptr)
                    {
                        _recvCb(_recvBuf);
                    }
                    _recvLen = 0;
                }
                continue;
            }

            if (_recvLen < sizeof(_recvBuf) - 1)
            {
                _recvBuf[_recvLen++] = ch;
            }
            else
            {
                // 单行超长：强制分发，避免缓冲区卡死
                _recvBuf[_recvLen] = '\0';
                if (_recvCb != nullptr)
                {
                    _recvCb(_recvBuf);
                }
                _recvLen = 0;
            }
        }

        /* ---------- 对端关闭 ---------- */
        if (!_client.connected() && _client.available() == 0)
        {
            Serial.println("[TCP] 连接已被对端关闭，准备重连");
            _client.stop();
            _connected = false;
            _recvLen = 0;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(got ? 2 : 10));
    }
}

bool ESP32TcpClient::send(const char *buf)
{
    if (buf == nullptr || !_connected)
    {
        return false;
    }
    size_t len = strlen(buf);
    if (len == 0)
    {
        return true;
    }
    return _client.write((const uint8_t *)buf, len) == len;
}

bool ESP32TcpClient::sendln(const char *buf)
{
    if (!_connected)
    {
        return false;
    }
    if (buf != nullptr && buf[0] != '\0' && !send(buf))
    {
        return false;
    }
    return _client.write((const uint8_t *)"\n", 1) == 1;
}

bool ESP32TcpClient::isConnected()
{
    return _wifiReady && _connected;
}

bool ESP32TcpClient::isWifiReady()
{
    return _wifiReady;
}

bool ESP32TcpClient::isTcpUp()
{
    return _connected;
}
