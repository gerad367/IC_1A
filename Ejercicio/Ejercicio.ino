// Práctica 1 parte 1

// Objetivos:
// [x] - 1. Ajustar el RTC a partir de la hora y fecha de generación del firmware.

// [x] - 2. Programar la alarma del RTC para simular la lectura de un sensor de forma periódica (cada
// 10 segundos). Cada vez que la alarma se active, se generará una cadena de texto con la fecha
// y la hora.

// [/] - 3. Esta cadena de texto deberá salvarse a un fichero que se habrá creado en el chip de memoria
// externa FLASH de la tarjeta.

// [x] - 4. Finalmente, poner el microcontrolador en modo sleep por tiempo indefinido. Se despertará
// cuando se active la alarma del RTC.

// [ ] - 5. Complementariamente, permitir que el microcontrolador pueda registrar otra interrupción
// (un flanco de bajada) a través de un pin digital, configurado como entrada en modo pull-up,
// cuando se lleva a tierra. Esta interrupción puede ocurrir en cualquier momento y el
// firmware deberá proceder como en el objetivo 2, pero indicando que esa línea se añade
// debido a una interrupción externa.

// Proceso para guardar la información en la memoria flash
// [x] - 1. Importar la librería y declarar el pin SPI
// [x] - 2. Desactivar el módulo LoRa
// [x] - 3. Inicializar la memoria flash
// [x] - 4. Borrar el chip
// [x] - 5. Formatear el sistema de ficheros
// [x] - 6. Montar el sistema de ficheros
// [x] - 7. Crear el fichero
// [x] - 8. Abrir el fichero
// [x] - 9. Escribir en el fichero
// [x] - 10. Cerrar el fichero

#include <ArduinoLowPower.h>

#include <Arduino_MKRMEM.h>
Arduino_W25Q16DV flash(SPI1, FLASH_CS);
char filename[] = "datos.txt";

#include <time.h>
#include <RTCZero.h>

#define ALARM_PERIOD_SECS 3

RTCZero rtc;

volatile uint16_t _rtcFlag = 0;
char dateBuff[64];
char dateTime[32];

void exit_error() {
  filesystem.unmount();
  exit(EXIT_FAILURE);
}

void setup()
{
  // Desactivamos el módulo LoRa
  pinMode(LORA_RESET, OUTPUT);
  digitalWrite(LORA_RESET, LOW);

  SerialUSB.begin(115200);
  while(!SerialUSB) {;}

  // Inicializamos la memoria flash
  flash.begin();

  // Borramos el chip y formateamos el FS
  flash.eraseChip();

  int res = filesystem.mount();
  if (res != SPIFFS_OK && res != SPIFFS_ERR_NOT_A_FS) {
    SerialUSB.println("mount() failed with error code "); SerialUSB.println(res); return;
  }

  SerialUSB.println("Unmounting ...");
  filesystem.unmount();

  // Formateamos el sistema de ficheros
  res = filesystem.format();
  if (res != SPIFFS_OK) {
    SerialUSB.print("format() failed with error code: ");
    SerialUSB.println(res);
    exit(EXIT_FAILURE);
  }

  // Montamos el sistema de ficheros
  res = filesystem.mount();
  if (res != SPIFFS_OK) {
    SerialUSB.print("mount() failed with error code: ");
    SerialUSB.println(res);
    exit(EXIT_FAILURE);
  }

  File file = filesystem.open(filename, CREATE | TRUNCATE);
  if (!file) {
    SerialUSB.print("Creation of file ");
    SerialUSB.print(filename);
    SerialUSB.print(" failed. Aborting ...");
    exit_error();
  }

  SerialUSB.print(__DATE__);
  SerialUSB.print(" ");
  SerialUSB.println(__TIME__);

  // Ajustamos que la alarma despierte al micro
  LowPower.attachInterruptWakeup(RTC_ALARM_WAKEUP, alarmCallback, CHANGE);

  // Habilitamos el uso del rtc
  rtc.begin();

  // Analizamos las dos cadenas para extraer fecha y hora y fijar el RTC
  if (!setDateTime(__DATE__, __TIME__))
  {
    SerialUSB.println("setDateTime() failed!\nExiting ...");
    while (1) { ; }
  }

  // Configuramos la alarma
  rtc.setAlarmEpoch(rtc.getEpoch() + ALARM_PERIOD_SECS);
  rtc.enableAlarm(rtc.MATCH_YYMMDDHHMMSS);
  rtc.attachInterrupt(alarmCallback);

  SerialUSB.println("Sleeping");
  LowPower.sleep();
}

void loop()
{
  if ( _rtcFlag ) {
    // Se ha activado la alarma. Se registra la lectura
    getDateTime();
    snprintf(dateBuff, sizeof(dateBuff), "Lectura por alarma: %s\n", dateTime);
    SerialUSB.println(dateBuff);

    // Abrimos el fichero para su lectura
    File file = filesystem.open(filename, WRITE_ONLY | APPEND);
    if (!file) {
      SerialUSB.print("Opening file ");
      SerialUSB.print(filename);
      SerialUSB.print(" failed for reading. Aborting ...");
      exit_error();
    }

    // Escribimos en el fichero
    int bytes_to_write = strlen(dateBuff);
    int bytes_written = file.write((void *)dateBuff, bytes_to_write);
    if (bytes_to_write != bytes_written) {
      SerialUSB.print("write() failed with error code "); 
      SerialUSB.println(filesystem.err());
      SerialUSB.println("Aborting ...");
      exit_error();
    }

    file.close();

    _rtcFlag--;
  }
  LowPower.sleep();
}


bool setDateTime(const char * date_str, const char * time_str)
{
  char month_str[4];
  char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  uint16_t i, mday, month, hour, min, sec, year;

  if (sscanf(date_str, "%3s %hu %hu", month_str, &mday, &year) != 3) return false;
  if (sscanf(time_str, "%hu:%hu:%hu", &hour, &min, &sec) != 3) return false;

  for (i = 0; i < 12; i++) {
    if (!strncmp(month_str, months[i], 3)) {
      month = i + 1;
      break;
    }
  }
  if (i == 12) return false;

  rtc.setTime((uint8_t)hour, (uint8_t)min, (uint8_t)sec);
  rtc.setDate((uint8_t)mday, (uint8_t)month, (uint8_t)(year - 2000));
  return true;
}


void getDateTime()
{
  const char *weekDay[7] = { "Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat" };

  // Obtenemos el tiempo Epoch, segundos desde el 1 de enero de 1970
  time_t epoch = rtc.getEpoch();

  // Convertimos a la forma habitual de fecha y hora
  struct tm stm;
  gmtime_r(&epoch, &stm);

  // Generamos e imprimimos la fecha y la hora
   
  snprintf(dateTime, sizeof(dateTime),"%s %4u/%02u/%02u %02u:%02u:%02u",
           weekDay[stm.tm_wday], 
           stm.tm_year + 1900, stm.tm_mon + 1, stm.tm_mday, 
           stm.tm_hour, stm.tm_min, stm.tm_sec);
}


void alarmCallback()
{
  _rtcFlag++;

  rtc.setAlarmEpoch(rtc.getEpoch() + ALARM_PERIOD_SECS);
}
