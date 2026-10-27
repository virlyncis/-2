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
  S_WARNING,
  NUM_STATES
};

enum Event {
  E_NONE,
  E_TIMER,
  E_PED,
  E_EMERGENCY_ON,
  E_EMERGENCY_OFF,
  E_NIGHT_TOGGLE,
  NUM_EVENTS
};

typedef void (*Handler)();

Handler fsmTable[NUM_STATES][NUM_EVENTS];

State currentState = S_GREEN;
State savedState = S_GREEN;

unsigned long stateStart = 0;
unsigned long stateDuration = GREEN_TIME;
unsigned long savedRemaining = GREEN_TIME;

bool pedRequest = false;
bool emergency = false;
bool nightMode = false;
bool warningBlinkOn = true;

// Антидребезг и распознавание длительного нажатия
bool lastRawButton = HIGH;
bool stableButton = HIGH;
unsigned long debounceStart = 0;
unsigned long pressStart = 0;
bool longPressHandled = false;

// Буфер команд Serial Monitor
char serialBuffer[32];
byte serialLength = 0;

const char* stateName(State s) {
  switch (s) {
    case S_GREEN:   return "GREEN";
    case S_YELLOW:  return "YELLOW";
    case S_RED:     return "RED";
    case S_WARNING: return "WARNING";
    default:        return "UNKNOWN";
  }
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

    default:
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

// Обработчики событий
void onPedestrian() {
  pedRequest = true;
  Serial.println("Event: PEDESTRIAN_REQUEST");
}

void onEmergencyOn() {
  if (!emergency) {
    emergency = true;
    Serial.println("Event: EMERGENCY_ON");
    enterWarning("Emergency ON");
  }
}

void onEmergencyOff() {
  if (emergency) {
    emergency = false;
    Serial.println("Event: EMERGENCY_OFF");
    restoreNormalState("Emergency OFF");
  }
}

void onNightToggle() {
  nightMode = !nightMode;

  Serial.print("Event: NIGHT_MODE_TOGGLE -> ");
  Serial.println(nightMode ? "ON" : "OFF");

  if (nightMode) {
    enterWarning("Night mode ON");
  } else {
    restoreNormalState("Night mode OFF");
  }
}

// Обработчики таймера для разных состояний
void onGreenTimer() {
  goToState(S_YELLOW, YELLOW_TIME,
            "Green timer expired");
}

void onYellowTimer() {
  if (pedRequest) {
    pedRequest = false;
    goToState(S_RED, PED_RED_TIME,
              "Pedestrian crossing");
  } else {
    goToState(S_RED, RED_TIME,
              "Yellow timer expired");
  }
}

void onRedTimer() {
  goToState(S_GREEN, GREEN_TIME,
            "Red timer expired");
}

void onWarningTimer() {
  warningBlinkOn = !warningBlinkOn;

  digitalWrite(YELLOW_LED,
               warningBlinkOn ? HIGH : LOW);

  stateStart = millis();
}

// Заполнение таблицы переходов
void setupTable() {
  for (byte s = 0; s < NUM_STATES; s++) {
    for (byte e = 0; e < NUM_EVENTS; e++) {
      fsmTable[s][e] = NULL;
    }
  }

  fsmTable[S_GREEN][E_TIMER] = onGreenTimer;
  fsmTable[S_YELLOW][E_TIMER] = onYellowTimer;
  fsmTable[S_RED][E_TIMER] = onRedTimer;
  fsmTable[S_WARNING][E_TIMER] = onWarningTimer;

  for (byte s = 0; s < NUM_STATES; s++) {
    fsmTable[s][E_PED] = onPedestrian;
    fsmTable[s][E_EMERGENCY_ON] = onEmergencyOn;
    fsmTable[s][E_EMERGENCY_OFF] = onEmergencyOff;
    fsmTable[s][E_NIGHT_TOGGLE] = onNightToggle;
  }
}

// Передача события обработчику из таблицы
void dispatch(Event ev) {
  if (ev == E_NONE) {
    return;
  }

  Handler h = fsmTable[currentState][ev];

  if (h != NULL) {
    h();
  }
}

// Опрос физической кнопки
void readButton() {
  bool reading = digitalRead(BUTTON_PIN);

  // 1. Фиксируем изменение сигнала кнопки
  if (reading != lastRawButton) {
    lastRawButton = reading;
    debounceStart = millis();
  }

  // 2. Устраняем дребезг контактов
  if (millis() - debounceStart >= DEBOUNCE_TIME) {
    if (reading != stableButton) {
      stableButton = reading;

      // Кнопку нажали
      if (stableButton == LOW) {
        pressStart = millis();
        longPressHandled = false;
      }

      // Кнопку отпустили
      else {
        // Короткое нажатие — только запрос пешехода
        if (!longPressHandled) {
          dispatch(E_PED);
        }
      }
    }
  }

  // 3. Длительное нажатие переключает ночной режим
  if (stableButton == LOW &&
      !longPressHandled &&
      millis() - pressStart >= LONG_PRESS_TIME) {

    longPressHandled = true;
    dispatch(E_NIGHT_TOGGLE);
  }
}

// Неблокирующий опрос Serial Monitor
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
      dispatch(E_EMERGENCY_ON);
    } else if (
      strcmp(serialBuffer, "EMERGENCY_OFF") == 0) {
      dispatch(E_EMERGENCY_OFF);
    } else {
      Serial.println("Unknown command");
    }
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

  setupTable();

  goToState(S_GREEN, GREEN_TIME, "System start");

  Serial.println("Commands: EMERGENCY_ON, EMERGENCY_OFF");
}

void loop() {
  readButton();
  readSerialCommands();

  if (millis() - stateStart >= stateDuration) {
    dispatch(E_TIMER);
  }
}