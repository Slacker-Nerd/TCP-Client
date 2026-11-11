#include <Arduino.h>
#include "TcpClient.h"

// 实例化
ESP32TcpClient tcp;

// 收到上位机数据回调
void onTcpRecv(const char *data)
{
    Serial.print("PC -> ESP: ");
    Serial.println(data);
    // 收到指令，可以在这里解析命令
    if (strcmp(data, "ping") == 0)
    {
        tcp.sendln("pong");
    }
}

void setup()
{
    Serial.begin(115200);
    // 参数：wifi名称，wifi密码，电脑内网IP，端口，回调函数，是否由本类管理 WiFi（示例中用 true）
    tcp.begin("你的WIFI", "WIFI密码", "192.168.1.100", 8080, onTcpRecv, true);
}

void loop()
{
    static unsigned long lastSend = 0;
    if (millis() - lastSend > 1000)
    {
        lastSend = millis();
        if (tcp.isConnected())
        {
            tcp.sendln("Heartbeat from ESP32C3");
        }
    }
    delay(100);
}
