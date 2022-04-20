/*
(c)saigon 2017-2020  
Written: Dec 09 2017.
Last Updated: Apr 20 2022
  
  Метеостанция с отправкой данных на narodmon.ru
  Отправляет данные каждые 5 минут. Обновляет данные на экране каждые 10 сек.
  Измеряет давление каждые 10 минут, хранит данные за 6 часов измерений
  Рисует анимированный график изменения давления за 6 часов с шагом 0,5 ммРст на одно деление
  При расчёте давления использовано среднее арифметическое из 10 измерений
  для уменьшения шумности сигнала с датчика

Connecting the BME280 and AHT21 Sensor:           Connecting the LCD I2C:
Sensor              ->  Board                 LCD                 ->  Board
-----------------------------                 -----------------------------
Vin (Voltage In)    ->  3.3V                  Vin (Voltage In)    ->  5V
Gnd (Ground)        ->  Gnd                   Gnd (Ground)        ->  Gnd
SDA (Serial Data)   ->  D2 on ESP             SDA (Serial Data)   ->  D2 on ESP
SCK (Serial Clock)  ->  D1 on ESP             SCK (Serial Clock)  ->  D1 on ESP
*/
 
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <BME280I2C.h>
#include <Adafruit_AHTX0.h>
#include <ESP8266WiFi.h>            //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>              //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>       //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>            //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#define SERIAL_BAUD 9600
#define postingInterval  300000     // Интервал между отправками данных в миллисекундах (5 минут)
#define LCDInterval  10000          // Интервал между обновлениями экрана в миллисекундах (10 сек)
#define pressureInterval  600000    // Интервал между считываниями давления в миллисекундах (10 минут)
#define animInterval  1000          // Шаг анимации графика изменения давления (1 сек)
#define start_pos 12                // Нижний левый угол графика (столбец)
#define row 1                       // Нижний левый угол графика (строка)
#define debug true                  // Вывод отладочных сообщений

const char* AP_NAME = "NarodMonAP";                   // SSID временной точки доступа
const char* AP_PASSWORD = "12345678";                 // Пароль
unsigned long lastConnectionTime = 0;                 // Время последней передачи данных
unsigned long lastLCDTime = 0;                        // Время последнего вывода данных на экран
unsigned long pressureTimer = 0;                      // Время последнего считывания давления в массив
unsigned long animTimer = 0;                          // Таймер анимации графика давления
float pres10_array[37];                               // Массив для хранения изменения давления за 6 часов каждые 10 минут
float pressure_array[7];                              // Массив для хранения изменения давления за 6 часов каждый час
int bar_array[6];                                     // Массив для хранения высоты столбика графика изменения давления
String Hostname;                                      // Имя железки - выглядит как ESPAABBCCDDEEFF т.е. ESP+mac адрес.
String place = "Рославль, ул.Ленина, 18";             // Адрес места нахождения железки для отображения в народном мониторинге
//String place = "Стародубский район, с.Левенка";     // Адрес места нахождения железки для отображения в народном мониторинге
float temp(NAN), hum(NAN), pres(NAN);                 // Переменные датчика
int a = 8;                                            // Переменная шага анимации. 1-7 рисуем график. 8 - очищаем график.

Adafruit_AHTX0 aht;                 // Инициализируем датчик AHT21
BME280I2C bme;                      // Default : forced mode, standby time = 1000 ms
                                    // Oversampling = pressure ×1, temperature ×1, humidity ×1, filter off,
LiquidCrystal_I2C lcd(0x3f,20,2);   // Экран 20 символов 2 строки - от модема Зелакс


//========================================================================

void setup(){

 lcd.init();      // initialize the lcd 
 lcd.clear();  
 lcd.setCursor(3,0);
 lcd.print("Check WiFi");
 lcd.setCursor(3,1);
 lcd.print(String(AP_NAME));
   
 wifimanstart();        // Вызываем процедуру подключения к WiFi
 lcd.clear();  
  
 Serial.begin(SERIAL_BAUD);
  while(!Serial) {}     // Wait
  Wire.begin();
  while(!bme.begin())   // Проверяем наличие датчика BME280
  {
    Serial.println("Could not find BME280 sensor!");
  lcd.setCursor(3,0);
  lcd.print("Could not find");
  lcd.setCursor(3,1);
  lcd.print("BME280 sensor!");
      delay(1000);
      lcd.clear();   
  }

  while(!aht.begin())   // Проверяем наличие датчика AHT21
  {
    Serial.println("Could not find AHT21 sensor!");
  lcd.setCursor(0,0);
  lcd.print("Could not find");
  lcd.setCursor(0,1);
  lcd.print("AHT21 sensor!");
      delay(1000);
      lcd.clear();   
  }
  if (aht.begin()) Serial.println("AHT10 or AHT20 found");
  
  
  switch(bme.chipModel())
  {
     case BME280::ChipModel_BME280:
       Serial.println("Found BME280 sensor! Success.");
       break;
     case BME280::ChipModel_BMP280:
       Serial.println("Found BMP280 sensor! No Humidity available.");
       break;
     default:
       Serial.println("Found UNKNOWN sensor! Error!");
  } 


initBar();       // инициализация символов для отрисовки графика. При использовании каких то своих символов,
                  // нужно вызывать эту функцию перед отрисовкой графика/полосы!

 // выдаем в компорт ip железки и ее id. этот id используется при регистрации датчика на сайте народного мониторинга
 Hostname = "ESP"+WiFi.macAddress();
 Hostname.replace(":","");
 Serial.println(WiFi.localIP()); Serial.println(WiFi.macAddress()); Serial.print("Narodmon ID: "); Serial.println(Hostname);

  aver_sens();                     // найти текущее давление по среднему арифметическому
  
  for (byte i = 0; i < 37; i++) {  // счётчик от 0 до 36
    pres10_array[i] = pres;        // забить весь массив текущим давлением
  }
  for (byte i = 0; i < 7; i++) {   // счётчик от 0 до 6
    pressure_array[i] = pres;      // забить весь массив текущим давлением
  }
  calcBar();                       // вызов функции пересчета графика изменения давления

 lastConnectionTime = millis() - postingInterval + 15000; //первая передача на народный мониторинг через 15 сек.
}
 
//========================================================================
void loop(){

// Прорисовываем анимированный график
 if (millis() - animTimer > animInterval) {

  if (a > 8) a = 1; 
  if (a == 8) {  // Очищаем область графика и рисуем шкалу справа
     lcd.setCursor(start_pos, row - 1);
       lcd.print("       ");
     lcd.setCursor(start_pos, row);
       lcd.print("       ");      
     lcd.setCursor(start_pos + 7, row - 1);
       lcd.write((byte)5);
     lcd.setCursor(start_pos + 7, row);
       lcd.write((byte)6);
         
  } else {
  for (byte i = 0; i < a; i++){
    if ( bar_array[i] <= 4 ){                 // Если высота столбца графика равна или меньше 4, внизу рисуем столбец нужной высоты, вверху рисуем чистую клетку
        lcd.setCursor(start_pos + i, row);
        lcd.write((byte)bar_array[i]);
        lcd.setCursor(start_pos + i, row - 1);
        lcd.write((byte)32);
    }
    if ( bar_array[i] > 4 ){                  // Если высота столбца графика больше 4, внизу рисуем закрашенную клетку, вверху рисуем столбец нужной высоты
        lcd.setCursor(start_pos + i, row);
        lcd.write((byte)4);
        lcd.setCursor(start_pos + i, row - 1);
        lcd.write((byte)bar_array[i] - 4);        
    }
  }
}
    a++;
    animTimer = millis();
}

// Обновляем массивы давления 
   if (millis() - pressureTimer > pressureInterval) { 

    for (byte i = 0; i < 37; i++) {                   // счётчик от 0 до 36
      pres10_array[i] = pres10_array[i + 1];          // сдвинуть массив давлений КРОМЕ ПОСЛЕДНЕЙ ЯЧЕЙКИ на шаг назад
    }
      pres10_array[36] = pres;                        // последний элемент массива теперь - новое давление

    for (byte i = 0; i < 7; i++) {                    // счётчик от 0 до 6 (да, до 6. Так как 6 меньше 7)
      pressure_array[i] = pres10_array[i * 6];        // заполнить массив новыми значениями из большого массива с шагом 6
                                                      // т.е. каждые 10 минут сдвигаем массив, получаем 6 часов показаний давления
    }
    
    calcBar();                                        // вызов функции пересчета графика изменения давления
    pressureTimer = millis();
      if (debug) Serial.println(String(pressure_array[6]));   // дебаг
   }


// Выводим данные на экран, соблюдая заданный интервал   
 if (millis() - lastLCDTime > LCDInterval) { // Соблюдаем заданный интервал вывода данных на экран  

  aver_sens(); // Вызываем функцию чтения данных с датчика

 //Отдаем строку с данными в ком порт
   if (debug) Serial.println("Temp:" + String(temp) + " °C    Humidity:" + String(hum) + "%    Pressure:" + String(pres) + " mmHg   " + millis()/1000);

  //Перед выводом на экран проверяем идут ли данные с BME280
  if (String(pres) == "nan"){ 

  // Print error message.
  lcd.clear();
  lcd.setCursor(3,0);
  lcd.print("Could not find");
  lcd.setCursor(3,1);
  lcd.print("BME280 sensor!");
   delay(1000);
   lcd.clear(); 

  } else {
       
  // Print a message to the LCD.
    lcd.setCursor(0,0);
  lcd.print("    ");
    lcd.setCursor(5-String(temp,1).length(), 0);
  lcd.print(String(temp,1));
  lcd.write((byte)223);   // Значек градуса
    lcd.setCursor(6,0);
  lcd.print(" " + String(hum,1) + "%");
    lcd.setCursor(3,1);
  lcd.print(String(pres,1) + "mm");
  lastLCDTime = millis();
  }
}


// Отправляем данные на народный мониторинг
  if (millis() - lastConnectionTime > postingInterval) { // Соблюдаем заданный интервал отправки 5 минут
      if (WiFi.status() == WL_CONNECTED && String(pres) != "nan") { // Если подключен WiFi и получены данные с датчика
      if (SendToNarodmon()) {
      lastConnectionTime = millis();
      }else{  lastConnectionTime = millis() - postingInterval + 15000; }//следующая попытка через 15 сек    
      }else{  lastConnectionTime = millis() - postingInterval + 15000; Serial.println("Not connected to WiFi"); lcd.clear(); lcd.print("No WiFi");}//следующая попытка через 15 сек
    } yield(); // Вызов этой функции передает управление другим задачам.

}

//====================== Функции ==================================================

// Формирование пакета и отправка.
bool SendToNarodmon() { 
    WiFiClient client;
    String buf;
    buf = "#" + Hostname + "#" + place + "\r\n"; // заголовок и ИМЯ, которое будет отображаться в народном мониторинге, чтоб не светить реальный мак адрес
    buf = buf + "#TEMP#" + String(temp) + "\r\n"; //температура
    buf = buf + "#HUMID#" + String(hum) + "\r\n"; //влажность
    buf = buf + "#PRESS#" + String(pres) + "\r\n"; //давление
    buf = buf + "##\r\n"; // закрываем пакет
 
    if (!client.connect("narodmon.ru", 8283)) { // попытка подключения
      Serial.println("Connection failed");
      lcd.clear();
      lcd.print("Connect failed");
      delay(3000);
      lcd.clear(); 
      return false; // не удалось;
    } else
    {
      client.print(buf); // и отправляем данные
      if (debug) Serial.print(buf);
      while (client.available()) {
        String line = client.readStringUntil('\r'); // если что-то в ответ будет - отправляем в Serial
        Serial.print(line);
        }
    }
      return true; //ушло
 }

// Считаем среднее арифметичеcкое от температуры, давления и влажности за 10 считываний
float aver_sens() {
   temp=NAN; hum=NAN; pres=NAN; 
   BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
   BME280::PresUnit presUnit(BME280::PresUnit_Pa);

  float pressureBME = 0;
  float presBME, tempBME, humBME;
  float temperatureAHT = 0, humidityAHT = 0;
  float tempAHT, humAHT;
  
  // Получаем среднее значение давления за 10 замеров с датчика BME280
  for (byte i = 0; i < 10; i++) { 
  bme.read(presBME, tempBME, humBME, tempUnit, presUnit); //Читаем данные с датчика
  pressureBME += presBME;
  }

  // Получаем среднее значение температуры и влажности за 10 замеров с датчика AHT21
  sensors_event_t humidityAHT21, tempAHT21;
  for (byte i = 0; i < 10; i++) {
  aht.getEvent(&humidityAHT21, &tempAHT21); //Читаем данные с датчика
  temperatureAHT += tempAHT21.temperature;
  humidityAHT += humidityAHT21.relative_humidity;
  }
  
  pres = pressureBME / 10;
  pres /= 133.3; // convert to mmHg
  temp = temperatureAHT / 10;
  hum = humidityAHT / 10;
}
 
void wifimanstart() { // Волшебная процедура начального подключения к Wifi.
                        // Если не знает к чему подцепить - создает точку доступа AP_NAME и настроечную таблицу http://192.168.4.1
                        // Подробнее: https://github.com/tzapu/WiFiManager
  WiFiManager wifiManager;
  wifiManager.setDebugOutput(debug);
  wifiManager.setMinimumSignalQuality();
  if (!wifiManager.autoConnect(AP_NAME, AP_PASSWORD)) {
  if (debug) Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000); }
if (debug) Serial.println("connected...");
}

// ---- Функция создания символов ----
void initBar() {
  // необходимые символы для работы
  // создано в http://maxpromer.github.io/LCD-Character-Creator/
  byte bar0[] = {B00000, B00000, B00000, B00000, B00000, B00000, B00000, B11111};
  byte bar1[] = {B00000, B00000, B00000, B00000, B00000, B00000, B11111, B11111};
  byte bar2[] = {B00000, B00000, B00000, B00000, B11111, B11111, B11111, B11111};
  byte bar3[] = {B00000, B00000, B11111, B11111, B11111, B11111, B11111, B11111};
  byte bar4[] = {B11111, B11111, B11111, B11111, B11111, B11111, B11111, B11111};
  byte grid1[] = {B11000, B10010, B10111, B10010, B10000, B10000, B10000, B10000};
  byte grid2[] = {B10000, B10000, B10000, B10000, B10000, B10111, B10000, B11000};
  lcd.createChar(0, bar0);
  lcd.createChar(1, bar1);
  lcd.createChar(2, bar2);
  lcd.createChar(3, bar3);
  lcd.createChar(4, bar4);
  lcd.createChar(5, grid1);
  lcd.createChar(6, grid2);
}

// ---- Функция расчета графика изменения давления ----
void calcBar() {
  
float barStep = 0.5;  //Количество mmHg на одно деление высоты столбика (х mmHg на одно деление из четырех)

   //Переводим давление в высоту столбцов ( выше или ниже относительно текущего давления)
   for (byte i = 0; i < 7; i++)  {
    if ( pressure_array[i] > pressure_array[6] + barStep * 3) bar_array[i] = 8;
    if ( pressure_array[i] <= pressure_array[6] + barStep * 3 && pressure_array[i] > pressure_array[6] + barStep * 2 ) bar_array[i] = 7;
    if ( pressure_array[i] <= pressure_array[6] + barStep * 2 && pressure_array[i] > pressure_array[6] + barStep ) bar_array[i] = 6;
    if ( pressure_array[i] <= pressure_array[6] + barStep && pressure_array[i] > pressure_array[6] ) bar_array[i] = 5;
    if ( pressure_array[i] <= pressure_array[6] && pressure_array[i] > pressure_array[6] - barStep ) bar_array[i] = 4;
    if ( pressure_array[i] <= pressure_array[6] - barStep && pressure_array[i] > pressure_array[6] - barStep * 2 ) bar_array[i] = 3;
    if ( pressure_array[i] <= pressure_array[6] - barStep * 2 && pressure_array[i] > pressure_array[6] - barStep * 3 ) bar_array[i] = 2;
    if ( pressure_array[i] <= pressure_array[6] - barStep * 3 && pressure_array[i] > pressure_array[6] - barStep * 4 ) bar_array[i] = 1;
    if ( pressure_array[i] <= pressure_array[6] - barStep * 4 ) bar_array[i] = 0;
       if (debug) Serial.println(String(bar_array[i]));
  }

}

/*
Массив с данными для наглядности отладки

360 350 340 330 320 310 300 290 280 270 260 250 240 230 220 210 200 190 180 170 160 150 140 130 120 110 100 90 80 70 60 50 40 30 20 10 0  Минуты
0   1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27 28 29 30 31 32 33 34 35 36 Массив

6  5  4  3  2  1  0   Часы
0  1  2  3  4  5  6   Массив

740
739
738
737
736======
735
734
733


   Еще один массив для отладки
pressure_array[0] = 717;
pressure_array[1] = 718;
pressure_array[2] = 719;
pressure_array[3] = 720;
pressure_array[4] = 728;
pressure_array[5] = 729;
pressure_array[6] = 730;
*/
