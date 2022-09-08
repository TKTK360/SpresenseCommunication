#include <Camera.h>

#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "person_detect_model.h"

#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "avator.h"
#include "avator2.h"
#include "cloud1.h"
#include "cloud2.h"
#include "cloud3.h"

//Button
#define BTN_PIN A0

//D0 : RX
//D1 : TX
#define CMD_DATE    1
#define CMD_WEATHER 2
#define CMD_LIGHT   5
#define CMD_SILENT  6
#define CMD_HELLO   7
#define CMD_BYE     8

// BUTTONN KEY ID
int push_index = 0;


//TFT
#define TFT_RST 8
#define TFT_DC  9
#define TFT_CS  10

//Dispaly
#define TFT_BACKLIGHT_PIN 7
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);

bool isTftLight = true;

int g_sendCmd = 0;

int g_weather = 0;
String  g_day = "2022/-/-";
String  g_time = "-:-";
String  g_tempMax = "-";
String  g_tempMin = "-";

//TF
tflite::ErrorReporter* error_reporter = nullptr;
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;
int inference_count = 0;

constexpr int kTensorArenaSize = 100000;
uint8_t tensor_arena[kTensorArenaSize];

// cropping and scaling parameters
const int offset_x = 32;
const int offset_y = 12;
const int width    = 320;
const int height   = 240;
const int target_w = 96;
const int target_h = 96;

uint16_t disp[width*height];
uint32_t last_mills = 0;

bool g_isPerson = false;


//--------------------------------------------
int getButtonKey()
{
  int index = 0;
  int data = analogRead(BTN_PIN);
  //Serial.println(data);

  if (5 <= data && data <= 70) {
    push_index = 1;
  } else if (90 <= data && data <= 150) {
    push_index = 2;
  } else if (300 <= data && data <= 350) {
    push_index = 3;
  } else if (360 <= data && data <= 500) {
    push_index = 4;
  } else if (530 <= data && data <= 700) {
    push_index = 5;
  } else {    
    if (push_index != 0) {
      index = push_index;
      push_index = 0;
      Serial.print("btn= ");
      Serial.println(index);
    }    
  }
  return index;
}


//--------------------------------------------
//setup_display
void setup_display() 
{
  //tft.begin(); 
  tft.begin(40000000);
  tft.setRotation(3);  
}

//disp_image
void disp_image(int weather) 
{
  //avator
  if (g_isPerson) {
    tft.drawRGBBitmap(320-200, 40, avator2, 200, 200); 
  } else {
    tft.drawRGBBitmap(320-145, 0, avator, 145, 240); 
  }
  yield();
  
  //weather
  //晴れ
  if (weather == 2) {
    tft.drawRGBBitmap(2, 240-82, cloud1, 70, 72); 
  }
  //雨
  else if (weather == 1) {
    tft.drawRGBBitmap(2, 240-82, cloud2, 70, 72); 
  }
  //くもり
  else if (weather == 3) {
    tft.drawRGBBitmap(2, 240-82, cloud3, 70, 72); 
  }
}


void disp_watch(String day, String time)
{
  tft.setTextColor(ILI9341_WHITE);  

  tft.setCursor(10, 38);
  tft.setTextSize(3);
  tft.print(day.c_str()); //"2022/9/3";
  
  tft.setCursor(10, 72);
  tft.setTextSize(4);
  tft.print(time.c_str());  //"20:08";
  yield();

  Serial.print("day=");
  Serial.println(day);
  Serial.print("time=");
  Serial.println(time);
}

void disp_temperature(String max, String min)
{
  tft.setTextColor(ILI9341_RED);  

  tft.setCursor(70, 175);
  tft.setTextSize(4);
  tft.print(max.c_str());  //"30";
  
  tft.setTextColor(ILI9341_WHITE);  
  tft.print("/");

  tft.setTextColor(ILI9341_BLUE);  
  tft.print(min.c_str());  //"24";
}

void disp_refresh()
{
  tft.setCursor(0, 0);
  tft.fillScreen(ILI9341_BLACK);
  yield();

  disp_image(g_weather);
  yield();
  disp_watch(g_day, g_time);
  yield();
  disp_temperature(g_tempMax, g_tempMin);
}


void disp_light()
{
  if (isTftLight)
  {
    Serial.println("light on");
    digitalWrite(TFT_BACKLIGHT_PIN,HIGH);

    disp_refresh();
  }
  else
  {
    Serial.println("light off");
    digitalWrite(TFT_BACKLIGHT_PIN,LOW);
  }

  isTftLight = !isTftLight;
}

//--------------------------------------------
void commandFunction(int cmd)
{
  if (g_sendCmd != 0){
    return;
  }
  
  if (cmd == CMD_DATE){
    Serial.println("date");
    Serial2.println("date");
    g_sendCmd = CMD_DATE;
  } else if (cmd == CMD_WEATHER){
    Serial.println("weather");
    Serial2.println("weather");
    g_sendCmd = CMD_WEATHER;
  } else if (cmd == CMD_LIGHT){
    //disp_light();
    disp_refresh();
    g_sendCmd = 0;
  } else if (cmd == CMD_SILENT){
    Serial.println("silent");
    Serial2.println("silent");
    g_sendCmd = CMD_SILENT;
  } else if (cmd == CMD_HELLO){
    Serial.println("hello");
    Serial2.println("hello");
    g_sendCmd = CMD_HELLO;
  } else if (cmd == CMD_BYE){
    Serial.println("bye");
    Serial2.println("bye");
    g_sendCmd = CMD_BYE;
  }
}

int Split(String data, char delimiter, String *dst)
{
    int index = 0;
    int arraySize = (sizeof(data)/sizeof((data)[0]));  
    int datalength = data.length();

    for (int i = 0; i < datalength; i++) {
        char tmp = data.charAt(i);
        if ( tmp == delimiter ) {
          index++;
          if ( index > (arraySize - 1)) 
            return -1;
        }
        else { 
          dst[index] += tmp;
        }
    }
}


void DrawDate(String data)
{
  //2022/9/6,22:18
  String cmds[3] = {"", "", "\0"}; 
  Split(data, ',', cmds);

  g_day = cmds[0];
  g_time = cmds[1];

  disp_refresh();
}


void DrawWeather(String data)
{
  //1,30,25
  String cmds[4] = {"", "", "", "\0"}; 
  Split(data, ',', cmds);

  g_weather = cmds[0].toInt();
  
  g_tempMax = cmds[1];
  g_tempMin = cmds[2];

  disp_refresh();
}

//--------------------------------------------
void CamCB(CamImage img) 
{
  uint32_t current_mills = millis();
  uint32_t duration = current_mills - last_mills;

  if (duration < 5000 || g_sendCmd != 0) {
    return;
  }


  Serial.println("start detect");
  tft.writeFillRect(20, 0, 20, 20, ILI9341_YELLOW);
  if (!img.isAvailable()) {
    Serial.println("img is not available");
    return;
  }

  CamImage small;

  CamErr err = img.resizeImageByHW(small, 160, 120);
  if (!small.isAvailable()) {
    Serial.println("small is not available");
    return;
  }

  //small.convertPixFormat(CAM_IMAGE_PIX_FMT_RGB565);

  // get image data from the frame memory
  uint16_t* buf = (uint16_t*)small.getImgBuff();   
  int n = 0; 
  for (int y = offset_y; y < offset_y + target_h; ++y) {
    for (int x = offset_x; x < offset_x + target_w; ++x) {
      // extracting luminance data from YUV422 data
      uint16_t value = buf[y*width + x];
      uint16_t y_h = (value & 0xf000) >> 8;
      uint16_t y_l = (value & 0x00f0) >> 4;
      value = (y_h | y_l);  /* luminance data */
      /* set the grayscale data to the input buffer for TensorFlow  */
      input->data.f[n++] = (float)(value)/255.0;
    }
  }

  Serial.println("detect");
  TfLiteStatus invoke_status = interpreter->Invoke();
  if (invoke_status != kTfLiteOk) {
    Serial.println("Invoke failed");
    return;
  }

  // get the result
  bool result = false;
  int8_t person_score = output->data.uint8[1];
  int8_t no_person_score = output->data.uint8[0];
  Serial.print("Person = " + String(person_score) + ", ");
  Serial.print("No_person = " + String(no_person_score));
  
  if ((person_score > no_person_score) && (person_score > 60)) {
    digitalWrite(LED3, HIGH);
    result = true;
    Serial.println(" : ON");
    if (!g_isPerson) {
      g_isPerson = true;
      tft.writeFillRect(0, 0, 20, 20, ILI9341_RED);
      commandFunction(CMD_HELLO);
      disp_refresh();
    }
  } else {
    digitalWrite(LED3, LOW);
    Serial.println(" : OFF");
    if (g_isPerson) {
      g_isPerson = false;
      tft.writeFillRect(0, 0, 20, 20, ILI9341_BLUE);
      commandFunction(CMD_BYE);
      disp_refresh();
    }
  }

  tft.writeFillRect(20, 0, 20, 20, ILI9341_BLACK);
  last_mills = millis();
}


//--------------------------------------------
void setup() 
{
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1);

  //------------------------------------------
  // Button
  pinMode(BTN_PIN, INPUT);
  //pinMode(TFT_BACKLIGHT_PIN,OUTPUT);

  //------------------------------------------
  // Display
  Serial.println("setup_display");
  setup_display();
  disp_light();

  //------------------------------------------
  // TF
  Serial.println("InitializeTarget");
  tflite::InitializeTarget();
  memset(tensor_arena, 0, kTensorArenaSize*sizeof(uint8_t));
  
  // Set up logging. 
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure..
  Serial.println("GetModel");
  model = tflite::GetModel(model_tflite);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model provided is schema version " 
                  + String(model->version()) + " not equal "
                  + "to supported version "
                  + String(TFLITE_SCHEMA_VERSION));
    return;
  } else {
    Serial.println("Model version: " + String(model->version()));
  }

  // This pulls in all the operation implementations we need.
  static tflite::AllOpsResolver resolver;
  
  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
          model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;
  
  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  
  if (allocate_status != kTfLiteOk) {
    Serial.println("AllocateTensors() failed");
    return;
  } else {
    Serial.println("AllocateTensor() Success");
  }

  size_t used_size = interpreter->arena_used_bytes();
  Serial.println("Area used bytes: " + String(used_size));
  input = interpreter->input(0);
  output = interpreter->output(0);

  Serial.println("Model input:");
  Serial.println("dims->size: " + String(input->dims->size));
  for (int n = 0; n < input->dims->size; ++n) {
    Serial.println("dims->data[" + String(n) + "]: " + String(input->dims->data[n]));
  }

  Serial.println("Model output:");
  Serial.println("dims->size: " + String(output->dims->size));
  for (int n = 0; n < output->dims->size; ++n) {
    Serial.println("dims->data[" + String(n) + "]: " + String(output->dims->data[n]));
  }

  Serial.println("Completed tensorflow setup");
  digitalWrite(LED0, HIGH); 

  
  Serial.println("theCamera.begin");
  CamErr err = theCamera.begin(1, CAM_VIDEO_FPS_5, width, height, CAM_IMAGE_PIX_FMT_YUV422);
  if (err != CAM_ERR_SUCCESS) {
    Serial.println("camera begin err: " + String(err));
    return;
  }
  err = theCamera.startStreaming(true, CamCB);
  if (err != CAM_ERR_SUCCESS) {
    Serial.println("start streaming err: " + String(err));
    return;
  }

  Serial.println("setup");
}


//--------------------------------------------
void loop() 
{
  // KEY CODE
  int key = getButtonKey();  
  commandFunction(key);
  
  if(Serial2.available() >0) 
  {
    String data = "";    
    Serial.println("Serial2 is available.");
    
    data = Serial2.readString();
    Serial.print("read = ");
    Serial.print(data);

    if (g_sendCmd == CMD_DATE || g_sendCmd == CMD_SILENT) {
      DrawDate(data);
    } else if (g_sendCmd == CMD_WEATHER) {
      DrawWeather(data);
    } 
    g_sendCmd = 0;
  }
  delay(200);
}
