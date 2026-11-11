#ifndef TcpClient_H
#define TcpClient_H

#include <Arduino.h>
#include <WiFi.h>

// 接收回调函数类型：收到一行数据（\n 结尾）时调用
// 注意：回调运行在 TCP 后台任务中，函数内禁止 delay / 长阻塞
typedef void (*TcpRecvCallback)(const char *data);

class ESP32TcpClient
{
public:
    ESP32TcpClient();
    ~ESP32TcpClient();

    /**
     * 初始化：创建后台任务并开始维护 TCP 连接
     * @param ssid/pwd       WiFi 账号（autoWifi=true 时才由本类使用）
     * @param pcIp/pcPort    服务器地址
     * @param cb             收到一行数据时的回调
     * @param autoWifi       true=本类负责 WiFi.begin；false=WiFi 由调用者管理（推荐，避免重复 begin 打断连接）
     *
     * 重复调用 begin() 不会重复创建任务，只会更新目标地址与回调。
     */
    void begin(const char *ssid, const char *pwd, const char *pcIp, uint16_t pcPort,
               TcpRecvCallback cb = nullptr, bool autoWifi = false);

    // 发送字符串（不追加换行）
    bool send(const char *buf);
    // 发送字符串 + "\n"
    bool sendln(const char *buf);

    // WiFi 已连上 且 TCP 已建立
    bool isConnected();
    bool isWifiReady();
    bool isTcpUp();

    // 切换服务器地址；地址变化时会主动断开旧连接，由后台任务重连
    void setTarget(const char *pcIp, uint16_t pcPort);

    // 当前目标地址
    const char *serverIp() const { return _serverIp; }
    uint16_t serverPort() const { return _pcPort; }

    // 停止任务并断开连接（可再次 begin 重启）
    void end();

    // 内部任务入口，不要上层调用
    void taskLoop();

private:
    void connectOnce();

    WiFiClient _client;
    const char *_ssid;
    const char *_pwd;

    char _serverIp[16]; // 内部拷贝，避免调用者 String 生命周期问题
    uint16_t _pcPort;
    TcpRecvCallback _recvCb;
    bool _autoWifi;

    volatile bool _wifiReady = false;
    volatile bool _connected = false;
    volatile bool _running = false;
    TaskHandle_t _taskHandle = nullptr;

    char _recvBuf[256]; // 单行接收缓冲
    size_t _recvLen = 0;
};

#endif
