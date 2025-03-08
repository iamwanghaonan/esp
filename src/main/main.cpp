#include <Arduino.h>
#include "base64.h"
#include "WiFi.h"
#include <WiFiClientSecure.h>
#include "HTTPClient.h"
#include "Recorder.h"
#include "Speaker.h"
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <MySQL_Cursor.h>

using namespace websockets;

WiFiClient client;
// MySQL database credentials
char *db_user = "root";            // 数据库用户名
char *db_password = "************"; // 数据库密码
const char *db_name = "memory";    // 数据库名
const char* db_host = "sh-cdb-lgll25s2.sql.tencentcdb.com";  // 新的数据库地址
int db_port = 24902;               // 新的数据库端口
IPAddress ip(106, 53, 64, 53); // 手动构造 IP 地址对象
MySQL_Connection conn((Client*)&client); // MySQL 连接对象
void smartConfigConnect();
void saveToDatabase(const String &speaker, const String &content) {
    if (conn.connected()) {
        // 确保连接使用 UTF-8 编码
        const char* setCharsetQuery = "SET NAMES utf8mb4";
        MySQL_Cursor *cursor = new MySQL_Cursor(&conn);
        cursor->execute(setCharsetQuery);  // 设置字符集为 utf8mb4
        
        // 准备 SQL 插入语句
        char query[2048];
        snprintf(query, sizeof(query),
                 "INSERT INTO memories (speaker, content) VALUES ('%s', '%s')",
                 speaker.c_str(), content.c_str());

        Serial.print("Executing query: ");
        Serial.println(query);

        // 执行查询
        if (cursor->execute(query)) {
            Serial.println("Conversation saved to database.");
        } else {
            Serial.println("Failed to execute query.");
        }

        delete cursor;
    } else {
        Serial.println("Database not connected. Failed to save conversation.");
    }
}

#define key 0
#define ADC 32
#define led3 2
#define led2 18
#define led1 19
const char *wifiData[][2] = {
    {"王浩楠的无敌大手机", "888888888"}, // 替换为自己常用的wifi名和密码
    {"我看过饺子坠落桌面", "1436528yuhao"},
    // 继续添加需要的 Wi-Fi 名称和密码
};

String APPID = "7d81c54e"; // 自己的星火大模型账号参数
String APIKey = "054fcdb867c515a0caa4bb04f1fd57ce";
String APISecret = "M2QzYmQ4MGVhMzBkNjQzN2FmNGViMGJl";

bool ledstatus = true;
bool startPlay = false;
bool lastsetence = false;
bool isReady = false;
unsigned long urlTime = 0;
unsigned long pushTime = 0;
int mainStatus = 0;
int receiveFrame = 0;
int noise = 50;
HTTPClient https;

hw_timer_t *timer = NULL;

uint8_t adc_start_flag = 0;
uint8_t adc_complete_flag = 0;

Recorder recorder;
Speaker speaker(false, 3, I2S_NUM_1);

#define I2S_DOUT 25 // DIN
#define I2S_BCLK 27 // BCLK
#define I2S_LRC 26  // LRC

void gain_token(void);
void getText(String role, String content);
void checkLen(JsonArray textArray);
int getLength(JsonArray textArray);
float calculateRMS(uint8_t *buffer, int bufferSize);
void ConnectChatServer();
void ConnectIatServer();

DynamicJsonDocument doc(4000);
JsonArray text = doc.to<JsonArray>();

String chat_url = "";
String iat_url = "";
String Date = "";
DynamicJsonDocument gen_params(const char *appid, const char *domain);

String askquestion = "";
String Answer = "";

const char *domain1 = "generalv3.5";
const char *chat_server = "ws://spark-api.xf-yun.com/v3.5/chat";//大语言模型的api
const char *iat_server = "ws://ws-api.xfyun.cn/v2/iat";//语音识别的api
/*
const char *domain1 = "generalv3";
const char *chat_server = "ws://spark-api.xf-yun.com/v3.5/chat";
const char *iat_server = "ws://ws-api.xfyun.cn/v2/iat";
*/
using namespace websockets;

WebsocketsClient chatSocketClient;
WebsocketsClient iatSocketClient;

int loopcount = 0;
void onChatMessageCallback(WebsocketsMessage message)
{
    StaticJsonDocument<4096> jsonDocument;
    DeserializationError error = deserializeJson(jsonDocument, message.data());

    if (!error)
    {
        int code = jsonDocument["header"]["code"];
        if (code != 0)
        {
            Serial.print("sth is wrong: ");
            Serial.println(code);
            Serial.println(message.data());
            chatSocketClient.close();
        }
        else
        {
            receiveFrame++;
            Serial.print("receiveFrame:");
            Serial.println(receiveFrame);
            JsonObject choices = jsonDocument["payload"]["choices"];
            int status = choices["status"];
            const char *content = choices["text"][0]["content"];
            Serial.println(content);
            Answer += content;
            String answer = "";
            if (Answer.length() >= 120 && (speaker.isplaying == 0))
            {
                String subAnswer = Answer.substring(0, 120);
                Serial.print("subAnswer:");
                Serial.println(subAnswer);
                int lastPeriodIndex = subAnswer.lastIndexOf("。");

                if (lastPeriodIndex != -1)
                {
                    answer = Answer.substring(0, lastPeriodIndex + 1);
                    Serial.print("answer: ");
                    Serial.println(answer);
                    Answer = Answer.substring(lastPeriodIndex + 2);
                    Serial.print("Answer: ");
                    Serial.println(Answer);
                    speaker.connecttospeech(answer.c_str(), "zh");
                }
                else
                {
                    const char *chinesePunctuation = "？，：；,.";

                    int lastChineseSentenceIndex = -1;

                    for (int i = 0; i < Answer.length(); ++i)
                    {
                        char currentChar = Answer.charAt(i);

                        if (strchr(chinesePunctuation, currentChar) != NULL)
                        {
                            lastChineseSentenceIndex = i;
                        }
                    }
                    if (lastChineseSentenceIndex != -1)
                    {
                        answer = Answer.substring(0, lastChineseSentenceIndex + 1);
                        speaker.connecttospeech(answer.c_str(), "zh");
                        Answer = Answer.substring(lastChineseSentenceIndex + 2);
                    }
                    else
                    {
                        answer = Answer.substring(0, 120);
                        speaker.connecttospeech(answer.c_str(), "zh");
                        Answer = Answer.substring(120 + 1);
                    }
                }
                startPlay = true;
            }

            if (status == 2)
            {
                getText("assistant", Answer);
                if (Answer.length() <= 80 && (speaker.isplaying == 0))
                {
                    // getText("assistant", Answer);
                    speaker.connecttospeech(Answer.c_str(), "zh");
                }
            }
        }
    }
}


void onChatEventsCallback(WebsocketsEvent event, String data)
{
    if (event == WebsocketsEvent::ConnectionOpened)
    {
        Serial.println("Send message to server0!");
        DynamicJsonDocument jsonData = gen_params(APPID.c_str(), domain1);
        String jsonString;
        serializeJson(jsonData, jsonString);
        Serial.println(jsonString);
        chatSocketClient.send(jsonString);
    }
    else if (event == WebsocketsEvent::ConnectionClosed)
    {
        Serial.println("Connnection0 Closed");
    }
    else if (event == WebsocketsEvent::GotPing)
    {
        Serial.println("Got a Ping!");
    }
    else if (event == WebsocketsEvent::GotPong)
    {
        Serial.println("Got a Pong!");
    }
}

// 注意：下面代码中对 MySQL_Cursor 的 API 用法可能需要根据你的具体库版本进行调整

void loadDatabaseMemoryContext(int maxMessages = 10) {
    String memoryText = "";
    if (conn.connected()) {
        // 查询最新的 N 条对话记录（按 id 倒序查询）
        char query[256];
        snprintf(query, sizeof(query),
                 "SELECT speaker, content FROM memories ORDER BY id DESC LIMIT %d", maxMessages);
        MySQL_Cursor *cursor = new MySQL_Cursor(&conn);
        cursor->execute(query);

        // 为了保持时间顺序，将结果先存入数组中再倒序添加到上下文
        // 这里假设 maxMessages 不大，直接分配固定数组
        String *messages = new String[maxMessages];
        int count = 0;
        column_names *cols = cursor->get_columns();
        row_values *row = NULL;
        while ((row = cursor->get_next_row()) != NULL && count < maxMessages) {
            // 组合成 "speaker: content" 的格式
            messages[count] = String(row->values[0]) + ": " + String(row->values[1]);
            count++;
        }
        delete cursor;

        // 将数组倒序（最早的消息在前）
        for (int i = count - 1; i >= 0; i--) {
            memoryText += messages[i] + "\n";
        }
        delete[] messages;
    } else {
        Serial.println("Database not connected when loading memory.");
    }
    
    // 如果记忆内容太长，则只保留后面的部分（这里限定最大长度为1000字符，可根据需要调整）
    const int MAX_MEMORY_LENGTH = 1000;
    if (memoryText.length() > MAX_MEMORY_LENGTH) {
        memoryText = memoryText.substring(memoryText.length() - MAX_MEMORY_LENGTH);
    }
    
    // 将记忆作为系统消息添加到上下文中，角色使用 "system"
    // 这里的 getText() 函数会将消息添加到全局的 JsonArray text 中
    getText("system", memoryText);
}

void onIatMessageCallback(WebsocketsMessage message)
{
    StaticJsonDocument<4096> jsonDocument;
    DeserializationError error = deserializeJson(jsonDocument, message.data());

    if (!error)
    {
        int code = jsonDocument["code"];
        if (code != 0)
        {
            Serial.println(code);
            Serial.println(message.data());
            iatSocketClient.close();
        }
        else
        {
            Serial.printf("xunfeiyun return message:%s\r\n",message.data());
            JsonArray ws = jsonDocument["data"]["result"]["ws"].as<JsonArray>();

            for (JsonVariant i : ws)
            {
                for (JsonVariant w : i["cw"].as<JsonArray>())
                {
                    askquestion += w["w"].as<String>();
                }
            }
            Serial.println(askquestion);
            bool isSaved = false;  // 用于标记是否已经保存过

if (askquestion.length() > 0)
{
    // 清理askquestion中的无用字符或空格
    askquestion.trim();  // 去掉前后的空格

    // 检查askquestion是否包含"记住"这个关键字
    if (askquestion.indexOf("记住") != -1 && !isSaved)  // 如果包含"记住"且尚未保存
    {
        if (conn.connected())
        {
            // 保存到数据库
            saveToDatabase("User", askquestion);  // 假设 "User" 是数据库字段，askquestion 是实际的内容
            Serial.println("Question saved to database.");
            isSaved = true;  // 标记为已保存，防止再次保存
        }
        else
        {
            Serial.println("Database is not connected when trying to save data.");
        }
    }
    else if (isSaved)
    {
        Serial.println("This question has already been saved.");
    }
    else
    {
        Serial.println("The question does not contain '记住', not saving to database.");
    }
}
else
{
    Serial.println("The user input is empty, not saving to database.");
}
            int status = jsonDocument["data"]["status"];
            if (status == 2)
            {
                Serial.println("status == 2");
                iatSocketClient.close();
                if (askquestion == "")
                {
                    askquestion = "sorry, i can't hear you";
                    speaker.connecttospeech(askquestion.c_str(), "zh");
                }
                else if (askquestion.substring(0, 9) == "唱歌了" or askquestion.substring(0, 9) == "唱歌啦")
                {

                    if (askquestion.substring(0, 12) == "唱歌了，" or askquestion.substring(0, 12) == "唱歌啦，")
                    { // 自建音乐服务器，按照文件名查找对应歌曲
                        String audioStreamURL = "http://192.168.0.1/mp3/" + askquestion.substring(12, askquestion.length() - 3) + ".mp3";
                        Serial.println(audioStreamURL.c_str());
                        speaker.connecttohost(audioStreamURL.c_str());
                    }
                    else if (askquestion.substring(9) == "。")
                    {
                        askquestion = "好啊, 你想听什么歌？";
                        mainStatus = 1;
                        speaker.connecttospeech(askquestion.c_str(), "zh");
                    }
                    else
                    {
                        String audioStreamURL = "http://192.168.0.1/mp3/" + askquestion.substring(9, askquestion.length() - 3) + ".mp3";
                        Serial.println(audioStreamURL.c_str());
                        speaker.connecttohost(audioStreamURL.c_str());
                    }
                }
                else if (mainStatus == 1)
                {
                    askquestion.trim();
                    if (askquestion.endsWith("。"))
                    {
                        askquestion = askquestion.substring(0, askquestion.length() - 3);
                    }
                    else if (askquestion.endsWith(".") or askquestion.endsWith("?"))
                    {
                        askquestion = askquestion.substring(0, askquestion.length() - 1);
                    }
                    String audioStreamURL = "http://192.168.0.1/mp3/" + askquestion + ".mp3";
                    Serial.println(audioStreamURL.c_str());
                    speaker.connecttohost(audioStreamURL.c_str());
                    mainStatus = 0;
                }
                else
                {
                    getText("user", askquestion);
                    Serial.print("text:");
                    Serial.println(text);
                    Answer = "";
                    lastsetence = false;
                    isReady = true;
                    ConnectChatServer();
                }
            }
        }
    }
    else
    {
        Serial.println("error:");
        Serial.println(error.c_str());
        Serial.println(message.data());
    }
}

void onIatEventsCallback(WebsocketsEvent event, String data)
{
    if (event == WebsocketsEvent::ConnectionOpened)
    {
        Serial.println("Send message to xunfeiyun");
        digitalWrite(led2, HIGH);
        int silence = 0;
        int firstframe = 1;
        int j = 0;
        int voicebegin = 0;
        int voice = 0;
        DynamicJsonDocument doc(2500);
        while (1)
        {
            doc.clear();
            JsonObject data = doc.createNestedObject("data");
            recorder.Record();
            float rms = calculateRMS((uint8_t *)recorder.wavData[0], 1280);
            printf("noise:%d rms:%f\n", noise, rms);
            if (rms < noise)
            {
                if (voicebegin == 1)
                {
                    silence++;
                    // Serial.print("noise:");
                    // Serial.println(noise);
                }
            }
            else
            {
                voice++;
                if (voice >= 5)
                {
                    voicebegin = 1;
                }
                else
                {
                    voicebegin = 0;
                }
                silence = 0;
            }
            if (silence == 6)
            {
                data["status"] = 2;
                data["format"] = "audio/L16;rate=8000";
                data["audio"] = base64::encode((byte *)recorder.wavData[0], 1280);
                data["encoding"] = "raw";
                j++;

                String jsonString;
                serializeJson(doc, jsonString);

                iatSocketClient.send(jsonString);
                digitalWrite(led2, LOW);
                delay(40);
                break;
            }
            if (firstframe == 1)
            {
                data["status"] = 0;
                data["format"] = "audio/L16;rate=8000";
                data["audio"] = base64::encode((byte *)recorder.wavData[0], 1280);
                data["encoding"] = "raw";
                j++;

                JsonObject common = doc.createNestedObject("common");
                common["app_id"] = APPID;

                JsonObject business = doc.createNestedObject("business");
                business["domain"] = "iat";
                business["language"] = "zh_cn";
                business["accent"] = "mandarin";
                business["vinfo"] = 1;
                business["vad_eos"] = 1000;

                String jsonString;
                serializeJson(doc, jsonString);

                iatSocketClient.send(jsonString);
                firstframe = 0;
                delay(40);
            }
            else
            {
                data["status"] = 1;
                data["format"] = "audio/L16;rate=8000";
                data["audio"] = base64::encode((byte *)recorder.wavData[0], 1280);
                data["encoding"] = "raw";

                String jsonString;
                serializeJson(doc, jsonString);

                iatSocketClient.send(jsonString);
                delay(40);
            }
        }
    }
    else if (event == WebsocketsEvent::ConnectionClosed)
    {
        Serial.println("Connnection1 Closed");
    }
    else if (event == WebsocketsEvent::GotPing)
    {
        Serial.println("Got a Ping!");
    }
    else if (event == WebsocketsEvent::GotPong)
    {
        Serial.println("Got a Pong!");
    }
}
void ConnectChatServer()
{
    Serial.println("chat_url:" + chat_url);

    chatSocketClient.onMessage(onChatMessageCallback);
    chatSocketClient.onEvent(onChatEventsCallback);
    // Connect to WebSocket
    Serial.println("Begin connect to chat server......");
    if (chatSocketClient.connect(chat_url.c_str()))
    {
        Serial.printf("Connected to chat server %s\r\n", chat_url);
    }
    else
    {
        Serial.printf("Failed to connect chat server %s\r\n", chat_url);
    }
}

void ConnectIatServer()
{
    // Serial.println("url1:" + url1);
    iatSocketClient.onMessage(onIatMessageCallback);
    iatSocketClient.onEvent(onIatEventsCallback);
    // Connect to WebSocket
    Serial.println("Begin connect to iat server......");
    if (iatSocketClient.connect(iat_url.c_str()))
    {
         Serial.printf("Connected to iat server %s\r\n",iat_url);
    }
    else
    {
        Serial.printf("Failed to connect iat server  %s\r\n", iat_url);
    }
}

void voicePlay()
{
    if ((speaker.isplaying == 0) && Answer != "")
    {
        // String subAnswer = "";
        // String answer = "";
        // if (Answer.length() >= 100)
        //     subAnswer = Answer.substring(0, 100);
        // else
        // {
        //     subAnswer = Answer.substring(0);
        //     lastsetence = true;
        //     // startPlay = false;
        // }

        // Serial.print("subAnswer:");
        // Serial.println(subAnswer);
        int firstPeriodIndex = Answer.indexOf("。");
        int secondPeriodIndex = 0;

        if (firstPeriodIndex != -1)
        {
            secondPeriodIndex = Answer.indexOf("。", firstPeriodIndex + 1);
            if (secondPeriodIndex == -1)
                secondPeriodIndex = firstPeriodIndex;
        }
        else
        {
            secondPeriodIndex = firstPeriodIndex;
        }

        if (secondPeriodIndex != -1)
        {
            String answer = Answer.substring(0, secondPeriodIndex + 1);
            Serial.print("answer: ");
            Serial.println(answer);
            Answer = Answer.substring(secondPeriodIndex + 2);
            speaker.connecttospeech(answer.c_str(), "zh");
        }
        else
        {
            const char *chinesePunctuation = "？，：；,.";

            int lastChineseSentenceIndex = -1;

            for (int i = 0; i < Answer.length(); ++i)
            {
                char currentChar = Answer.charAt(i);

                if (strchr(chinesePunctuation, currentChar) != NULL)
                {
                    lastChineseSentenceIndex = i;
                }
            }

            if (lastChineseSentenceIndex != -1)
            {
                String answer = Answer.substring(0, lastChineseSentenceIndex + 1);
                speaker.connecttospeech(answer.c_str(), "zh");
                Answer = Answer.substring(lastChineseSentenceIndex + 2);
            }
        }
        startPlay = true;
    }
    else
    {
        // digitalWrite(led3, LOW);
    }
}

void wifiConnect(const char *wifiData[][2], int numNetworks)
{
    WiFi.disconnect(true);
    for (int i = 0; i < numNetworks; ++i)
    {
        const char *ssid = wifiData[i][0];
        const char *password = wifiData[i][1];

        Serial.printf("Connecting to wifi:%s\r\n", ssid);
        // Serial.println(ssid);

        WiFi.begin(ssid, password);
        uint8_t count = 0;
        while (WiFi.status() != WL_CONNECTED)
        {
            digitalWrite(led1, ledstatus);
            ledstatus = !ledstatus;
            Serial.print(".");
            count++;
            if (count >= 30)
            {
                Serial.printf("\r\n-- wifi connect fail! try to use smartconfig --");
                break;
            }
            vTaskDelay(100);
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.printf("\r\n-- wifi connect success! --\r\n");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
            Serial.println("Free Heap: " + String(ESP.getFreeHeap()));
            return; // 如果连接成功，退出函数
        }
    }
    smartConfigConnect();
    digitalWrite(led1, HIGH);
}

void smartConfigConnect() {
    Serial.begin(115200);
    
    // 初始化WiFi为AP+STA模式，并开启SmartConfig
    WiFi.mode(WIFI_AP_STA);
    WiFi.beginSmartConfig();

    // 等待 SmartConfig 配网完成
    Serial.println("Waiting for SmartConfig...");
    while (!WiFi.smartConfigDone()) {
        delay(500);
        Serial.print(".");
        digitalWrite(led1, ledstatus);
        ledstatus = !ledstatus;
    }

    Serial.println("\nSmartConfig received.");

    // 等待 WiFi 连接
    Serial.println("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

}

String getUrl(String Spark_url, String host, String path, String Date)
{

    // 拼接字符串
    String signature_origin = "host: " + host + "\n";
    signature_origin += "date: " + Date + "\n";
    signature_origin += "GET " + path + " HTTP/1.1";
    // signature_origin="host: spark-api.xf-yun.com\ndate: Mon, 04 Mar 2024 19:23:20 GMT\nGET /v3.5/chat HTTP/1.1";

    // hmac-sha256 加密
    unsigned char hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    const size_t messageLength = signature_origin.length();
    const size_t keyLength = APISecret.length();
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)APISecret.c_str(), keyLength);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)signature_origin.c_str(), messageLength);
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    // base64 编码
    String signature_sha_base64 = base64::encode(hmac, sizeof(hmac) / sizeof(hmac[0]));

    // 替换Date
    Date.replace(",", "%2C");
    Date.replace(" ", "+");
    Date.replace(":", "%3A");
    String authorization_origin = "api_key=\"" + APIKey + "\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"" + signature_sha_base64 + "\"";
    String authorization = base64::encode(authorization_origin);
    String url = Spark_url + '?' + "authorization=" + authorization + "&date=" + Date + "&host=" + host;
    Serial.println(url);
    return url;
}

void getTimeFromServer()
{
    String timeurl = "https://www.baidu.com";
    HTTPClient http;
    http.begin(timeurl);
    const char *headerKeys[] = {"Date"};
    http.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));
    int httpCode = http.GET();
    Date = http.header("Date");
    Serial.println(Date);
    http.end();
    // delay(50); // 可以根据实际情况调整延时时间
}

void setup()
{
    // String Date = "Fri, 22 Mar 2024 03:35:56 GMT";
    Serial.begin(115200);
    // pinMode(ADC,ANALOG);
    pinMode(key, INPUT_PULLUP);
    pinMode(34, INPUT_PULLUP);
    pinMode(35, INPUT_PULLUP);
    pinMode(led1, OUTPUT);
    pinMode(led2, OUTPUT);
    pinMode(led3, OUTPUT);
    recorder.init();

    int numNetworks = sizeof(wifiData) / sizeof(wifiData[0]);
    wifiConnect(wifiData, numNetworks);
    // 将域名转换为 IP 地址
    IPAddress serverIP;
    if (WiFi.hostByName(db_host, serverIP)) {
    Serial.print("Resolved IP: ");
    Serial.println(serverIP);
    } else {
    Serial.println("Failed to resolve domain name");
    return;
    }

    // 连接到数据库
    if (conn.connect(serverIP, db_port, db_user, db_password, (char*)db_name)) {
    Serial.println("Connected to database!");
    // 准备设置字符集的 SQL 语句
    char query[2048];
    snprintf(query, sizeof(query), "SET NAMES utf8mb4");
    Serial.print("Executing character set SQL query: ");
    Serial.println(query);
    MySQL_Cursor* cursor = new MySQL_Cursor(&conn);
    if (cursor->execute(query)) {
        Serial.println("Character set set via SQL query.");
    } else {
        Serial.println("Failed to set character set via SQL query.");
    }
    delete cursor;
    } else {
    Serial.println("Failed to connect to database.");
    }

    getTimeFromServer();

    speaker.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    speaker.setVolume(50);

    // String Date = "Fri, 22 Mar 2024 03:35:56 GMT";
    chat_url = getUrl(chat_server, "spark-api.xf-yun.com", "/v3.5/chat", Date);
    iat_url = getUrl(iat_server, "ws-api.xfyun.cn", "/v2/iat", Date);
    urlTime = millis();
    loadDatabaseMemoryContext(10);

    ///////////////////////////////////
}

void loop()
{

    chatSocketClient.poll();
    iatSocketClient.poll();
    // delay(10);
    if (startPlay)
    {
        voicePlay();
    }

    speaker.loop();

    if (speaker.isplaying == 1)
    {
        digitalWrite(led3, HIGH);
    }
    else
    {
        digitalWrite(led3, LOW);
        if ((urlTime + 240000 < millis()) && (speaker.isplaying == 0))
        {
            urlTime = millis();
            getTimeFromServer();
            chat_url = getUrl(chat_server, "spark-api.xf-yun.com", "/v3.5/chat", Date);
            iat_url = getUrl(iat_server, "ws-api.xfyun.cn", "/v2/iat", Date);
        }
    }

    if (digitalRead(key) == 0)
    {
        speaker.isplaying = 0;
        startPlay = false;
        isReady = false;
        Answer = "";
        Serial.printf("Start recognition\r\n\r\n");

        adc_start_flag = 1;
        // Serial.println(esp_get_free_heap_size());

        if (urlTime + 240000 < millis()) // 超过4分钟，重新做一次鉴权
        {
            urlTime = millis();
            getTimeFromServer();
            chat_url = getUrl(chat_server, "spark-api.xf-yun.com", "/v3.5/chat", Date);
            iat_url = getUrl(iat_server, "ws-api.xfyun.cn", "/v2/iat", Date);
        }
        askquestion = "";
        // audio2.connecttospeech(askquestion.c_str(), "zh");
        ConnectIatServer();
        // ConnServer();
        // delay(6000);
        // audio1.Record();
        adc_complete_flag = 0;

        // Serial.println(text);
        // checkLen(text);
    }
}

void getText(String role, String content)
{
    checkLen(text);
    DynamicJsonDocument jsoncon(1024);
    jsoncon["role"] = role;
    jsoncon["content"] = content;
    text.add(jsoncon);
    jsoncon.clear();
    String serialized;
    serializeJson(text, serialized);
    Serial.print("text: ");
    Serial.println(serialized);
    // serializeJsonPretty(text, Serial);
}

int getLength(JsonArray textArray)
{
    int length = 0;
    for (JsonObject content : textArray)
    {
        const char *temp = content["content"];
        int leng = strlen(temp);
        length += leng;
    }
    return length;
}

int getNonSystemLength(JsonArray textArray) {
    int length = 0;
    for (JsonObject msg : textArray) {
        const char* role = msg["role"];
        if (strcmp(role, "system") != 0) {
            const char* content = msg["content"];
            length += strlen(content);
        }
    }
    return length;
}

void checkLen(JsonArray textArray) {
    int systemLength = 0;
    // 计算系统消息总长度
    for (JsonObject msg : textArray) {
         const char* role = msg["role"];
         if (strcmp(role, "system") == 0) {
             const char* content = msg["content"];
             systemLength += strlen(content);
         }
    }
    // 规定整个消息内容总长度上限为3000字符
    const int TOTAL_LIMIT = 3000;
    // 允许其他（非系统）消息的总长度
    int allowedNonSystemLength = TOTAL_LIMIT - systemLength;
    // 如果非系统消息部分超过限制，则依次删除最早添加的非系统消息
    while (getNonSystemLength(textArray) > allowedNonSystemLength && textArray.size() > 1) {
         // 遍历找到第一条非 system 的消息并移除
         for (int i = 0; i < textArray.size(); i++) {
             JsonObject msg = textArray[i];
             if (strcmp(msg["role"], "system") != 0) {
                  textArray.remove(i);
                  break;
             }
         }
    }
}

DynamicJsonDocument gen_params(const char *appid, const char *domain)
{
    DynamicJsonDocument data(2048);

    JsonObject header = data.createNestedObject("header");
    header["app_id"] = appid;
    header["uid"] = "1234";

    JsonObject parameter = data.createNestedObject("parameter");
    JsonObject chat = parameter.createNestedObject("chat");
    chat["domain"] = domain;
    chat["temperature"] = 0.5;
    chat["max_tokens"] = 2048;
    // 构造 payload 部分
        JsonObject payload = data.createNestedObject("payload");
    JsonObject message = payload.createNestedObject("message");

    // 构造 text 数组
        JsonArray textArray = message.createNestedArray("text");

    // 添加 system 角色消息（第一条必须是 system）
        JsonObject systemMessage = textArray.createNestedObject();
    systemMessage["role"] = "system";
    systemMessage["content"] = "你现在是一个面向健忘症老人的语音助手，帮助记录老人所说的物品摆放位置以及要做的事，请每次回复不超过50个字来回答用户。";

    // 添加历史对话（如果有）
    for (const auto &item : text)
    {
        textArray.add(item);
    }
    return data;
}

float calculateRMS(uint8_t *buffer, int bufferSize)
{
    float sum = 0;
    int16_t sample;

    for (int i = 0; i < bufferSize; i += 2)
    {

        sample = (buffer[i + 1] << 8) | buffer[i];
        sum += sample * sample;
    }

    sum /= (bufferSize / 2);

    return sqrt(sum);
}
