const byte RED_LED = 13;
const byte YELLOW_LED = 12;
const byte GREEN_LED = 11;
const byte BUTTON_PIN = 2;

const unsigned long GREEN_TIME = 10000UL;
const unsigned long YELLOW_TIME = 3000UL;
const unsigned long RED_TIME = 10000UL;
const unsigned long PED_RED_TIME = 15000UL;
const unsigned long BLINK_TIME = 500UL;
const unsigned long DEBOUNCE_TIME = 50UL;
const unsigned long LONG_PRESS_TIME = 800UL;

enum State {
  S_GREEN,
  S_YELLOW,
  S_RED,
  S_WARNING
};

enum Event {
  E_NONE,
  E_PED,
  E_NIGHT_TOGGLE,
  E_EMERGENCY_ON,
  E_EMERGENCY_OFF
};

State currentState = S_GREEN;
State savedState = S_GREEN;

unsigned long stateStart = 0;
unsigned long stateDuration = GREEN_TIME;
unsigned long savedRemaining = GREEN_TIME;

bool pedRequest = false;
bool emergency = false;
bool nightMode = false;
bool warningBlinkOn = true;

// Переменные антидребезга
bool lastRawButton = HIGH;
bool stableButton = HIGH;
unsigned long debounceStart = 0;
unsigned long pressStart = 0;
bool buttonPressActive = false;
bool longPressHandled = false;

// Буфер для команд Serial Monitor
char serialBuffer[32];
byte serialLength = 0;

const char* stateName(State s) {
  switch (s) {
    case S_GREEN:   return "GREEN";
    case S_YELLOW:  return "YELLOW";
    case S_RED:     return "RED";
    case S_WARNING: return "WARNING";
  }
  return "UNKNOWN";
}

void setOutputs(State s) {
  digitalWrite(RED_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  switch (s) {
    case S_GREEN:
      digitalWrite(GREEN_LED, HIGH);
      break;

    case S_YELLOW:
      digitalWrite(YELLOW_LED, HIGH);
      break;

    case S_RED:
      digitalWrite(RED_LED, HIGH);
      break;

    case S_WARNING:
      digitalWrite(YELLOW_LED,
                   warningBlinkOn ? HIGH : LOW);
      break;
  }
}

void goToState(State s, unsigned long duration,
               const char* reason) {
  currentState = s;
  stateStart = millis();
  stateDuration = duration;

  if (s == S_WARNING) {
    warningBlinkOn = true;
  }

  setOutputs(s);

  Serial.print(millis());
  Serial.print(" ms | State: ");
  Serial.print(stateName(s));
  Serial.print(" | Reason: ");
  Serial.println(reason);
}

void enterWarning(const char* reason) {
  // Сохраняем цикл только при первом входе
  // в предупреждение.
  if (currentState != S_WARNING) {
    unsigned long elapsed = millis() - stateStart;

    savedState = currentState;

    if (elapsed < stateDuration) {
      savedRemaining = stateDuration - elapsed;
    } else {
      savedRemaining = 1;
    }

    goToState(S_WARNING, BLINK_TIME, reason);
  }
}

void restoreNormalState(const char* reason) {
  if (currentState == S_WARNING &&
      !emergency && !nightMode) {
    goToState(savedState, savedRemaining, reason);
  }
}

void handleEvent(Event ev) {
  switch (ev) {
    case E_PED:
      pedRequest = true;
      Serial.println("Event: PEDESTRIAN_REQUEST");
      break;

    case E_NIGHT_TOGGLE:
      nightMode = !nightMode;

      Serial.print("Event: NIGHT_MODE_TOGGLE -> ");
      Serial.println(nightMode ? "ON" : "OFF");

      if (nightMode) {
        enterWarning("Night mode ON");
      } else {
        restoreNormalState("Night mode OFF");
      }
      break;

    case E_EMERGENCY_ON:
      if (!emergency) {
        emergency = true;
        Serial.println("Event: EMERGENCY_ON");
        enterWarning("Emergency ON");
      }
      break;

    case E_EMERGENCY_OFF:
      if (emergency) {
        emergency = false;
        Serial.println("Event: EMERGENCY_OFF");
        restoreNormalState("Emergency OFF");
      }
      break;

    case E_NONE:
      break;
  }
}

// Чтение одной физической кнопки
void readButton() {
  unsigned long now = millis();
  bool raw = digitalRead(BUTTON_PIN);

  // Фиксируем изменение физического сигнала
  if (raw != lastRawButton) {
    lastRawButton = raw;
    debounceStart = now;
  }

  // Обрабатываем сигнал после устранения дребезга
  if (now - debounceStart >= DEBOUNCE_TIME &&
      raw != stableButton) {

    stableButton = raw;

    // Нажатие: INPUT_PULLUP означает LOW
    if (stableButton == LOW) {
      pressStart = now;
      buttonPressActive = true;
    }

    // Отпускание кнопки
    else if (buttonPressActive) {
      unsigned long heldTime = now - pressStart;
      buttonPressActive = false;

      if (heldTime >= LONG_PRESS_TIME) {
        handleEvent(E_NIGHT_TOGGLE);
      } else {
        handleEvent(E_PED);
      }
    }
  }
}

// Неблокирующее чтение команд Serial Monitor
void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c != '\n') {
      if (serialLength < sizeof(serialBuffer) - 1) {
        serialBuffer[serialLength++] = c;
      } else {
        serialLength = 0;
        Serial.println("Error: command too long");
      }
      continue;
    }

    serialBuffer[serialLength] = '\0';
    serialLength = 0;

    if (strcmp(serialBuffer, "EMERGENCY_ON") == 0) {
      handleEvent(E_EMERGENCY_ON);
    } else if (
      strcmp(serialBuffer, "EMERGENCY_OFF") == 0) {
      handleEvent(E_EMERGENCY_OFF);
    } else {
      Serial.println("Unknown command");
    }
  }
}

// Основная логика переходов switch-case
void handleTimer() {
  unsigned long now = millis();

  switch (currentState) {
    case S_GREEN:
      if (now - stateStart >= stateDuration) {
        goToState(S_YELLOW, YELLOW_TIME,
                  "Green timer expired");
      }
      break;

    case S_YELLOW:
      if (now - stateStart >= stateDuration) {
        if (pedRequest) {
          pedRequest = false;
          goToState(S_RED, PED_RED_TIME,
                    "Pedestrian crossing");
        } else {
          goToState(S_RED, RED_TIME,
                    "Yellow timer expired");
        }
      }
      break;

    case S_RED:
      if (now - stateStart >= stateDuration) {
        goToState(S_GREEN, GREEN_TIME,
                  "Red timer expired");
      }
      break;

    case S_WARNING:
      if (now - stateStart >= BLINK_TIME) {
        warningBlinkOn = !warningBlinkOn;

        digitalWrite(YELLOW_LED,
                     warningBlinkOn ? HIGH : LOW);

        stateStart = now;
      }
      break;
  }
}

void setup() {
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(9600);

  lastRawButton = digitalRead(BUTTON_PIN);
  stableButton = lastRawButton;

  if (stableButton == LOW) {
    pressStart = millis();
  }

  goToState(S_GREEN, GREEN_TIME, "System start");

  Serial.println("Commands: EMERGENCY_ON, EMERGENCY_OFF");
}

void loop() {
  readButton();
  readSerialCommands();

  // Во время предупреждения обычный цикл остановлен
  if (emergency || nightMode) {
    handleTimer();
    return;
  }

  handleTimer();
}