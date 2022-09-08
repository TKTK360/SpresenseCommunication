#include <WiFi.h>
#include <time.h>
#include <ArduinoJson.h>

//Speaker
#include <HTTPClient.h>
#include <base64.h>       // for http basic auth 
#include <driver/dac.h>   // Audio dac 

#define JST     3600* 9

// Your WiFi credentials.
const char* ssid = "XXXXXX";
const char* pass = "XXXXXX";

String retData;

//-----------------------------------------------------------------------------------------------
// Weather
// 愛知県西部の天気予報を取得
const char* endpoint = "https://www.drk7.jp/weather/json/23.js";
const char* region = "西部";
DynamicJsonDocument weatherInfo(20000);

//-----------------------------------------------------------------------------------------------
// VoiceText Web API
uint16_t data16;          // wav data 16bit(2 bytes)
uint8_t  left;            // Audio dac voltage

// You should get apikey
// visit https://cloud.voicetext.jp/webapi
const String tts_url = "https://api.voicetext.jp/v1/tts";
const String tts_user = "XXXXX SET YOUR ID";
const String tts_pass = "";  // passwd is blank
uint16_t delayus = 60;  // depends on the sampling rate
uint8_t wavHeadersize = 44;  // depends on the wav format 
String tts_parms ="&speaker=show&volume=200&speed=120"; // he has natural(16kHz) wav voice


//------------------------------------------------------------------
// play 16bit wav data 
void playWav16(uint8_t * buffPlay, int len)
{
    for( int i=0 ; i<len; i+=sizeof(data16)) {
      memcpy(&data16, (char*)buffPlay + i, sizeof(data16));    
      left = ((uint16_t) data16 + 32767) >> 8;  // convert 16bit to 8bit
      dac_output_voltage(DAC_CHANNEL_1, left);
      ets_delay_us(delayus);
    }
}

//------------------------------------------------------------------
// text to speech
void text2speech(char * text)
{
    Serial.println("text to speech");
    
    if ((WiFi.status() == WL_CONNECTED))
    { //Check the current connection status
      HTTPClient http;  // Initialize the client library
      size_t size = 0; // available streaming data size
      
      http.begin(tts_url); //Specify the URL
      
      Serial.println("\nStarting connection to tts server...");
  
      //request header for VoiceText Web API
      String auth = base64::encode(tts_user + ":" + tts_pass);
      http.addHeader("Authorization", "Basic " + auth);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      
      String request = String("text=") + URLEncode(text) + tts_parms;
      http.addHeader("Content-Length", String(request.length()));

      //Make the request
      int httpCode = http.POST(request);
  
      if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          Serial.printf("[HTTP] POST... code: %d\n", httpCode);
          
          // file found at server
          if (httpCode == HTTP_CODE_OK) {
              // get lenght of data (is -1 when Server sends no Content-Length header)
              int len = http.getSize();
              Serial.printf("lenght of data: %d\n", len);

              // create buffer for read
              uint8_t buff[128] = { 0 };
              int buffPoint = 0;

              // get tcp stream
              WiFiClient * stream = http.getStreamPtr();
              
              // read wav header from server
              while(size < wavHeadersize && http.connected() && (len > 0 || len == -1)) {
                  // get available data size
                  size = stream->available();
              }
              
              if (size >= wavHeadersize) {
                  int c = stream->readBytes(buff, wavHeadersize);
                  if (strncmp((char*)buff + wavHeadersize -8, "data", 4)) {
                     Serial.printf("Error: wav file\n");
                     return;
                  }
                  
                  if (len >= wavHeadersize )
                     len -=wavHeadersize;                     
              } else {
                  Serial.printf("Error: wav file\n");
                  return;
              }
              
              Serial.printf("wav header confirmed\n");

              // read streaming data from server
              while (http.connected() && (len > 0 || len == -1)) {
                // get available data size
                size = stream->available();
                if (size > 0 ) {
                    int buffLeft = sizeof(buff)-buffPoint;
                    int c = stream->readBytes(buff+buffPoint, ((size > buffLeft) ? buffLeft : size ));
                    //Serial.printf("read stream size: %d\n",c);    
                    buffPoint += c;
                    if (len >=0)
                        len -= c;

                    if (buffPoint >= sizeof(buff)) {
                        playWav16(buff, buffPoint);
                        buff[0] = buff[buffPoint-1];
                        buffPoint = buffPoint % sizeof(data16);
                    }
                 }
              }
              
              if (buffPoint > sizeof(data16)) {
                playWav16(buff, buffPoint);
              }
              Serial.printf("len: %d  buffPoint: %d\n",len,buffPoint);    
          }
          
          Serial.println("finish play");          
        } else {
          Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        
        http.end();  //Free resources        
    } else {
        Serial.println("Error in WiFi connection");
    }
    
    dac_output_voltage(DAC_CHANNEL_1, 0);
}

//------------------------------------------------------------------
//日付
String GetDate()
{
  time_t t;
  struct tm *tm;

  t = time(NULL);
  tm = localtime(&t);

  String date = String(tm->tm_year+1900);
  retData = date;  
  date += "年";
  retData += "/";

  date += String(tm->tm_mon+1);
  retData += String(tm->tm_mon+1);
  date += "月";
  retData += "/";

  date += String(tm->tm_mday);
  retData += String(tm->tm_mday);
  date += "日";
  retData += ",";

  date += String(tm->tm_hour);
  retData += String(tm->tm_hour);
  date += "時";
  retData += ":";
  
  date += String(tm->tm_min);
  retData += String(tm->tm_min);
  date += "分";

  date += String(tm->tm_sec);
  date += "秒";

  return date;
}

//------------------------------------------------------------------
//天気
DynamicJsonDocument getJson() 
{
  DynamicJsonDocument doc(20000);

  if ((WiFi.status() == WL_CONNECTED)) {
      HTTPClient http;
      http.begin(endpoint);
      int httpCode = http.GET();
      if (httpCode > 0) {
          //jsonオブジェクトの作成
          String jsonString = createJson(http.getString());
          deserializeJson(doc, jsonString);
      } else {
          Serial.println("Error on HTTP request");
      }
      http.end(); //リソースを解放
  }
  return doc;
}

// JSONP形式からJSON形式に変える
String createJson(String jsonString)
{
  jsonString.replace("drk7jpweather.callback(","");
  return jsonString.substring(0,jsonString.length()-2);
}


String drawWeather(String infoWeather) 
{
  String data = "今日の天気は、";
      
  DynamicJsonDocument doc(20000);
  deserializeJson(doc, infoWeather);
  String weather = doc["weather"];
  retData = "2,";

  if (weather.indexOf("雨") != -1) {
      if (weather.indexOf("くもり") != -1) {
          data += "雨のち曇り";
      } else {
          data += "雨";
      }
      retData = "1,";
  } else if (weather.indexOf("晴") != -1) {
      if (weather.indexOf("くもり") != -1) {
          data += "晴れのち曇り";
      } else {
          data += "晴れ";
      }
      retData = "2,";
  } else if (weather.indexOf("くもり") != -1) {
      data += "曇り";
      retData = "3,";
  } else if (weather.indexOf("雪") != -1) {
      data += "雪";
      retData = "4,";
  } else {
      retData = "5,";
  }
  
  String maxTemperature = doc["temperature"]["range"][0]["content"];
  String minTemperature = doc["temperature"]["range"][1]["content"];

  data += "、最高気温は";
  data += maxTemperature;
  data += "度、最低気温は";
  data += minTemperature;
  data += "度です。";

  retData += maxTemperature;
  retData += ",";
  retData += minTemperature;

  return data;
}


String GetWeather()
{
  weatherInfo = getJson();

  String today = weatherInfo["pref"]["area"][region]["info"][0];
  return drawWeather(today);
}

//------------------------------------------------------------------
// from http://hardwarefun.com/tutorials/url-encoding-in-arduino
// modified by chaeplin
String URLEncode(const char* msg) 
{
  const char *hex = "0123456789ABCDEF";
  String encodedMsg = "";

  while (*msg != '\0') {
    if ( ('a' <= *msg && *msg <= 'z')
         || ('A' <= *msg && *msg <= 'Z')
         || ('0' <= *msg && *msg <= '9')
         || *msg  == '-' || *msg == '_' || *msg == '.' || *msg == '~' ) {
      encodedMsg += *msg;      
    } else {
      encodedMsg += '%';
      encodedMsg += hex[*msg >> 4];
      encodedMsg += hex[*msg & 0xf];      
    }    
    msg++;
  }  
  return encodedMsg;
}


//------------------------------------------------------------------
//Hello
String GetHello()
{
  retData = "hello";
  return "こんにちは！";
}

//------------------------------------------------------------------
//Bye
String GetBye()
{
  retData = "bye";
  return "さようなら！";
}

//------------------------------------------------------------------
//setup
void setup() 
{
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1);
  Serial.println("setup-start");

  // Speaker
  dac_output_enable(DAC_CHANNEL_1); // use DAC_CHANNEL_1 (pin 25 fixed)
  dac_output_voltage(DAC_CHANNEL_1, 0);
  
  // WiFi setup
  WiFi.mode(WIFI_STA);  // Disable Access Point
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("Connected to the WiFi network");
  
  Serial.println("configTime");
  configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

  Serial.println("setup-end");
}


//------------------------------------------------------------------
//loop
void loop()
{
  //受信
  if(Serial2.available() > 0) {    
    String data = Serial2.readString();
    Serial.print("get= ");
    Serial.println(data.c_str());

    //Command Get
    //[date]
    if(data.indexOf("date") >= 0){
      data = GetDate();
    }
    else if(data.indexOf("silent") >= 0){
      GetDate();
      data = "";
    }
    //[weather]
    else if(data.indexOf("weather") >= 0) {
      data = GetWeather();
    }
    //[hello]
    else if(data.indexOf("hello") >= 0) {
      data = GetHello();
    }
    //[bye]
    else if(data.indexOf("bye") >= 0) {
      data = GetBye();
    }
    else {
      return;
    }

    //send
    Serial2.println(retData.c_str());

    //speak
    if (data.length() > 1) {
      Serial.println(data.c_str());
      text2speech((char *)data.c_str());
    }
  }
}
