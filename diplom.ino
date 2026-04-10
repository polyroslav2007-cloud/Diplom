#include <Keypad.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Servo.h>
#include <EEPROM.h>

#define SS_PIN 10
#define RST_PIN 9
#define SERVO_PIN A1
#define RED_LED A2
#define GREEN_LED A3

#define CARD_COUNT_ADDR 0
#define CARDS_START_ADDR 1
#define MAX_CARDS 20

byte masterCard[4] = { 0xA9, 0x59, 0xFB, 0x06 };

MFRC522 rfid(SS_PIN, RST_PIN);
Servo servo;

const byte ROWS = 4, COLS = 3;
char keys[ROWS][COLS] = { { '#', '0', '*' }, { '9', '8', '7' }, { '6', '5', '4' }, { '3', '2', '1' } };
byte rowPins[ROWS] = { 5, 6, 7, 8 };
byte colPins[COLS] = { 2, 3, 4 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String input = "";
bool progMode = false, waitCard = false, waitDeleteByCard = false;
byte uid[4];

void setup() {
  Serial.begin(9600);

  if (EEPROM.read(CARD_COUNT_ADDR) > MAX_CARDS) {
    EEPROM.write(CARD_COUNT_ADDR, 0);
    Serial.println("EEPROM инициализирована");
  }

  SPI.begin();
  rfid.PCD_Init();
  servo.attach(SERVO_PIN);
  servo.write(0);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  Serial.println("СИСТЕМА ЗАПУЩЕНА");
  Serial.println("1234# - открыть дверь");
  Serial.println("0000# + мастер-карта - настройки");
}

void loop() {
  char key = keypad.getKey();
  if (key) handleKey(key);

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    for (byte i = 0; i < 4; i++) uid[i] = rfid.uid.uidByte[i];

    if (waitCard) checkMaster();
    else if (waitDeleteByCard) deleteByCard();
    else if (progMode) addCard();
    else checkAccess();

    rfid.PICC_HaltA();
  }

  if (servo.read() == 90) {
    delay(3000);
    lockDoor();
  }
}

void handleKey(char k) {
  if (progMode && !waitCard && !waitDeleteByCard) {
    if (k == '#') execCommand();
    else if (k >= '0' && k <= '9') input += k;
    else if (k == '*') input = "";
    return;
  }

  if (k == '#') {
    if (input == "0000") {
      Serial.println("Приложите мастер-карту");
      waitCard = true;
      blink(GREEN_LED, 3);
    } else if (input == "1234") {
      grantAccess();
    } else {
      denyAccess();
    }
    input = "";
  } else if (k == '*') {
    input = "";
    Serial.println("Ввод очищен");
  } else if (input.length() < 4) {
    input += k;
  }
}

void checkMaster() {
  bool ok = true;
  for (byte i = 0; i < 4; i++)
    if (uid[i] != masterCard[i]) ok = false;

  if (ok) {
    progMode = true;
    waitCard = false;
    Serial.println("=== РЕЖИМ НАСТРОЙКИ ===");
    Serial.println("1 - добавить карту");
    Serial.println("2 - удалить карту по номеру");
    Serial.println("3 - удалить карту поднесением");
    Serial.println("4 - показать список");
    Serial.println("0 - выход");
    blink(GREEN_LED, 3);
  } else {
    Serial.println("Неверная мастер-карта");
    denyAccess();
  }
}

void execCommand() {
  if (input == "1") {
    Serial.println("Приложите новую карту");
  } else if (input == "2") {
    Serial.println("Введите номер карты для удаления");
    input = "";
    waitForNumber();
    return;
  } else if (input == "3") {
    Serial.println("Приложите карту для удаления");
    waitDeleteByCard = true;
  } else if (input == "4") {
    showCards();
  } else if (input == "0") {
    progMode = false;
    Serial.println("Выход из режима настройки");
  } else if (input.length() > 0 && input[0] >= '0' && input[0] <= '9') {
    deleteByNumber(input.toInt());
  } else {
    Serial.println("Неверная команда");
  }
  input = "";
}

void waitForNumber() {
  Serial.println("Введите номер и нажмите #");
  while (true) {
    char k = keypad.getKey();
    if (k) {
      if (k >= '0' && k <= '9') {
        input += k;
        Serial.print("*");
      } else if (k == '#') {
        Serial.println();
        deleteByNumber(input.toInt());
        input = "";
        break;
      } else if (k == '*') {
        Serial.println("\nОтмена");
        input = "";
        break;
      }
    }
  }
}

void deleteByNumber(int num) {
  int cnt = EEPROM.read(CARD_COUNT_ADDR);
  if (num < 1 || num > cnt) {
    Serial.print("Ошибка: карты №");
    Serial.print(num);
    Serial.println(" не существует");
    return;
  }

  for (int i = num - 1; i < cnt - 1; i++) {
    for (byte j = 0; j < 4; j++) {
      EEPROM.write(CARDS_START_ADDR + i * 4 + j,
                   EEPROM.read(CARDS_START_ADDR + (i + 1) * 4 + j));
    }
  }

  for (byte j = 0; j < 4; j++) {
    EEPROM.write(CARDS_START_ADDR + (cnt - 1) * 4 + j, 0);
  }

  EEPROM.write(CARD_COUNT_ADDR, cnt - 1);
  Serial.print("Карта №");
  Serial.print(num);
  Serial.println(" удалена");
  blink(GREEN_LED, 2);
}

void deleteByCard() {
  Serial.print("UID карты для удаления: ");
  for (byte i = 0; i < 4; i++) {
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  int cnt = EEPROM.read(CARD_COUNT_ADDR);
  int foundIndex = -1;

  for (int i = 0; i < cnt; i++) {
    bool match = true;
    for (byte j = 0; j < 4; j++) {
      if (EEPROM.read(CARDS_START_ADDR + i * 4 + j) != uid[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      foundIndex = i;
      break;
    }
  }

  if (foundIndex == -1) {
    Serial.println("Карта не найдена в списке");
    denyAccess();
  } else {
    for (int i = foundIndex; i < cnt - 1; i++) {
      for (byte j = 0; j < 4; j++) {
        EEPROM.write(CARDS_START_ADDR + i * 4 + j,
                     EEPROM.read(CARDS_START_ADDR + (i + 1) * 4 + j));
      }
    }

    for (byte j = 0; j < 4; j++) {
      EEPROM.write(CARDS_START_ADDR + (cnt - 1) * 4 + j, 0);
    }

    EEPROM.write(CARD_COUNT_ADDR, cnt - 1);
    Serial.println("Карта удалена");
    blink(GREEN_LED, 2);
  }
  waitDeleteByCard = false;
}

void addCard() {
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != masterCard[i]) {
      if (!cardExists()) {
        int cnt = EEPROM.read(CARD_COUNT_ADDR);
        if (cnt < MAX_CARDS) {
          for (byte j = 0; j < 4; j++) {
            EEPROM.write(CARDS_START_ADDR + cnt * 4 + j, uid[j]);
          }
          EEPROM.write(CARD_COUNT_ADDR, cnt + 1);
          Serial.print("Карта добавлена. Всего карт: ");
          Serial.println(cnt + 1);
          blink(GREEN_LED, 1);
        } else {
          Serial.println("Лимит карт (10)");
        }
      } else {
        Serial.println("Эта карта уже есть в списке");
      }
      progMode = false;
      Serial.println("Возврат в обычный режим");
      return;
    }
  }
  Serial.println("Нельзя добавить мастер-карту");
}

void showCards() {
  int cnt = EEPROM.read(CARD_COUNT_ADDR);
  Serial.println("=== СПИСОК КАРТ ===");
  Serial.print("Всего: ");
  Serial.println(cnt);
  if (cnt == 0) {
    Serial.println("Список пуст");
    return;
  }

  for (int i = 0; i < cnt; i++) {
    Serial.print(i + 1);
    Serial.print(": ");
    for (byte j = 0; j < 4; j++) {
      byte b = EEPROM.read(CARDS_START_ADDR + i * 4 + j);
      if (b < 0x10) Serial.print("0");
      Serial.print(b, HEX);
      if (j < 3) Serial.print(":");
    }
    Serial.println();
  }
}

bool cardExists() {
  int cnt = EEPROM.read(CARD_COUNT_ADDR);
  for (int i = 0; i < cnt; i++) {
    bool match = true;
    for (byte j = 0; j < 4; j++) {
      if (EEPROM.read(CARDS_START_ADDR + i * 4 + j) != uid[j]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

void checkAccess() {
  bool isMaster = true;
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != masterCard[i]) {
      isMaster = false;
      break;
    }
  }

  if (isMaster) {
    Serial.println("Мастер-карта (открытие двери)");
    grantAccess();
  } else if (cardExists()) {
    grantAccess();
  } else {
    denyAccess();
  }
}

void grantAccess() {
  Serial.println("ДОСТУП РАЗРЕШЁН");
  digitalWrite(GREEN_LED, HIGH);
  servo.write(90);
}

void denyAccess() {
  Serial.println("ДОСТУП ЗАПРЕЩЁН");
  digitalWrite(RED_LED, HIGH);
  delay(2000);
  digitalWrite(RED_LED, LOW);
}

void lockDoor() {
  servo.write(0);
  digitalWrite(GREEN_LED, LOW);
  Serial.println("Дверь закрыта");
}

void blink(int pin, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(150);
    digitalWrite(pin, LOW);
    delay(150);
  }
}