# Architektura Fiesty

[English](ARCHITECTURE.md) · [O projekcie](README.pl.md)

Fiesta zastępuje i rozszerza elektronikę Forda Fiesty 1.8 (M)TDDI. Cztery
moduły firmware dzielą zadania w samochodzie: ECU steruje silnikiem,
a pozostałe obsługują wskaźniki, dodatkowe pomiary, czas i pomiar położenia
pompy. SerialConfigurator łączy się z nimi przez USB, odczytuje i zmienia
ustawienia oraz wgrywa firmware.

Osobny VP37TestBench obsługuje pompę na stanowisku. Ten dokument opisuje
powiązania między modułami. Polecenia programistyczne zawiera
[instrukcja przygotowania i kompilacji](doc/BUILDING.pl.md), a szczegóły
sterowania znajdują się w dokumentacji modułów.

## Układ systemu

```text
                 CAN0 (główna)
   ┌───────────┐ ◄──────────► Clocks          wskaźniki, TFT, olej, prędkość, EGT, GPS
   │    ECU    │ ◄──────────  Fiesta_clock    czas RTC w UTC, temperatury powietrza
   │  RP2350,  │
   │ 2 rdzenie │ ◄── I²C ───  Adjustometer    położenie VP37 (0x57)
   │           │ ─── I²C ───► PCF8574         przekaźniki (0x38)
   │           │ ─── CAN1 ──► gniazdo OBD-II
   └─────┬─────┘ ◄─ ADC, GPIO, PWM ─► czujniki i elementy wykonawcze
         │
         │ USB CDC, poza samochodem
         ▼
   Fiesta Serial Configurator (Linux, GTK4)
     łączy się też z Clocks i Fiesta_clock
```

ECU jako jedyny moduł steruje silnikiem. Wymagania MISRA-C obejmują ECU,
współdzielony moduł VP37 i Adjustometer. Pozostałe moduły dostarczają dane
lub prezentują to, co publikuje ECU.

Dobór płytki do firmware i pozostałe prace integracyjne opisuje [stan projektu](doc/STATUS.pl.md).

## Sprzęt

ECU pracuje na Waveshare RP2350-Plus. Jego PCB przyjmuje też RP2040-Plus o tym samym układzie
złączy; `device_tree.json` zawiera oba montaże z identycznym okablowaniem, domyślny jest
`rp2350-plus`. Pozostałe moduły używają płytek RP2040: Raspberry Pi Pico i RP2040-Zero. Każdy
moduł opisuje płytkę, urządzenia i piny w `device_tree.json`. Build generuje z niego
`jh_hardware.h`, a kod czyta makra `JH_HW_*`.

Inicjalizacja MCP2515 używa wygenerowanej konfiguracji każdego węzła,
w tym magistrali SPI, chip select, bitrate i częstotliwości oscylatora.
Fiesta_clock odczytuje też ustawienia `pull` wejść; polaryzacja GPIO
pochodzi z `activeLow`.

RP2040 i RP2350 wybrano ze względu na cenę, dostępność, dwa rdzenie, GPIO
z przerwaniami, PWM i możliwość emulacji EEPROM we flash. RP2350 daje ECU
szybsze rdzenie z jednostką zmiennoprzecinkową i więcej SRAM. Tablica korekcji
ADC RP2040 działa tylko w buildach RP2040; kody ADC RP2350 są używane wprost. Firmware korzysta z JaszczurHAL.
Przeniesienie na inny MCU wymaga również odpowiedniego backendu HAL, opisu
montażu, sprawdzenia czasu wykonania i pamięci oraz walidacji sprzętowej.

Schematy, projekty PCB i mapy złączy są w [`Fiesta_pcbs/`](Fiesta_pcbs/).
[`pinout.txt`](Fiesta_pcbs/pinout.txt) opisuje złącze ECU ze 104 pinami,
a [`wirings.txt`](Fiesta_pcbs/wirings.txt) kolory przewodów wiązki.

## Moduły

| Moduł | Język | Zadania | MISRA |
|---|---|---|---|
| [ECU](src/ECU/README.pl.md) | C | Silnik, diagnostyka i elementy wykonawcze. | Wymagany; VP37 ma bramkę bez aktywnych znalezisk, reszta raport analizy. |
| [Clocks](src/Clocks/README.pl.md) | C++ | Zestaw wskaźników; olej, prędkość, temperatury spalin i GPS. | Poza zakresem. |
| [Fiesta_clock](src/Fiesta_clock/README.pl.md) (`RTC_Clock`) | C | RTC ustawiany według UTC z GPS; czas i temperatury powietrza przez CAN. | Poza zakresem. |
| [Adjustometer](src/Adjustometer/README.pl.md) | C | Pomiar położenia VP37, slave I²C. | Wymagany, bramka bez aktywnych znalezisk. |
| [VP37TestBench](src/VP37TestBench/README.pl.md) | C, C++ | Współdzielony napęd VP37 na stanowisku. | Bramka obejmuje współdzielony VP37, własne pliki stanowiska są poza zakresem. |
| [SerialConfigurator](src/SerialConfigurator/README.pl.md) | C, GTK4 | Konfiguracja i wgrywanie firmware. | Poza zakresem. |

[`modules.json`](modules.json) określa rodzaj, token, pozycję konfiguratora
i katalog testów każdego modułu. Pozycja konfiguratora mówi też, czy moduł
dostaje podzakładkę Tests (`tests`) i czy status jego testów niesie położenie
nastawnika (`testsDrive`). Dodawanie modułu zaczyna się od tego rejestru.
[`scripts/fiesta_modules.py`](scripts/fiesta_modules.py) sprawdza go i generuje
nagłówek tokenów w `src/common/scDefinitions`, tabelę konfiguratora oraz listy
dla skryptów powłoki. Korzystają z nich generator VS Code, `runalltests.sh`,
`runmefirst.sh`, bootstrap i konfigurator. Indeks pozycji konfiguratora jest
jawny, ponieważ wiąże zapisane ścieżki obrazów z modułem. Listy w workflow CI
są zapisane bezpośrednio, a test porównuje je z rejestrem.

[`legacy/`](legacy/) zawiera wycofane DPF_main, AdaptiveLights i Fading.
Nie są kompilowane ani testowane.

### ECU

Rdzeń 0 obsługuje komunikację, diagnostykę, pamięć i wyjścia pomocnicze. Rdzeń 1 prowadzi
regulację VP37, RPM i turbo. Nie ma RTOS. [Przewodnik ECU](src/ECU/README.pl.md) prowadzi do
uruchomienia, opisu regulatora i szczegółów synchronizacji.

### Clocks

Steruje fabrycznymi zegarami, lampkami, TFT i buzzerem. Mierzy olej, prędkość ABS i EGT,
odbiera GPS; dane silnika otrzymuje z ECU przez CAN. [Instrukcja
Clocks](src/Clocks/README.pl.md) opisuje wskazania, utratę danych, testy wyjść i podział
pracy.

### Adjustometer

Mierzy położenie nastawnika przez częstotliwość oscylatora i przekazuje je przez I²C pod
adresem `0x57`. ECU wymaga świeżego, spójnego bloku sprzężenia; przed kalibracją czeka na
pomiar bazowy do 8 s (`VP37_ADJUSTOMETER_BASELINE_WAIT_MS`). [Instrukcja
czujnika](src/Adjustometer/README.pl.md) opisuje start, LED i rejestry. Moduł nie obsługuje
protokołu configuratora.

### VP37TestBench

Osobna płytka stanowiska używa wspólnego regulatora VP37 z własnym adapterem sprzętowym i
wyświetlaczem. [Przewodnik](src/VP37TestBench/README.pl.md) wyjaśnia różnicę względem BENCH
ECU.

### Fiesta_clock

Utrzymuje RTC, synchronizuje go z czasem GPS otrzymanym od Clocks i publikuje czas UTC oraz
temperatury przez CAN. Ekran i ustawianie czasu używają CET/CEST. [Instrukcja
zegara](src/Fiesta_clock/README.pl.md) opisuje warianty RTC, wskazania i odzyskiwanie czasu.

### Serial Configurator

Aplikacja C/GTK4 dla Linuksa wykrywa moduły USB, pokazuje tożsamość,
odczytuje i zmienia parametry, uruchamia testy obrazu stanowiskowego ECU
i testy Clocks oraz wgrywa firmware po sprawdzeniu manifestu. CLI obsługuje wykrywanie,
ustawienia, GPS, testy i uwierzytelnione wejście do bootloadera.
Pełne kopiowanie UF2 i ponowne połączenie zapewnia GUI.

Biblioteka rdzenia obsługuje porty, protokół, uwierzytelnianie, parametry
i wgrywanie, bez kodu GUI. Warstwa GTK prezentuje dane; nie otwiera portów
ani nie parsuje ramek. Od systemu zależą wykrywanie urządzeń, hot-plug,
wyszukiwanie dysku UF2, lokalizacja konfiguracji i pakowanie. Działa Linux;
Windows 10/11 jest planowany, ale transport szeregowy pozostaje POSIX.
Polecenia i ograniczenia opisuje [README](src/SerialConfigurator/README.pl.md).

## Kod współdzielony

- **[JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL)** jest osobnym
  repozytorium w submodule `src/JaszczurHAL`. `runmefirst.sh` inicjalizuje
  commit zapisany przez Fiestę. HAL zapewnia GPIO, ADC, PWM, I²C, SPI, CAN,
  timery, PID, KV na emulowanym EEPROM, logowanie i mock do testów hostowych.
  Moduł implementuje `app_start()`, `app_task0()` i opcjonalnie `app_task1()`.
- **[canDefinitions](src/common/canDefinitions/canDefinitions.h)** zawiera
  identyfikatory CAN, układ ramek i skalowanie danych między modułami.
- **[scDefinitions](src/common/scDefinitions/)** zawiera
  [protokół konfiguratora](src/common/scDefinitions/PROTOCOL.pl.md).
  `sc_config_session_t` z [sc_command_handlers.h](src/common/scDefinitions/sc_command_handlers.h)
  uruchamia sesję szeregową, obsługę poleceń SC i adapter między nimi.
  Plik `config` modułu określa parametry, wartości i callbacki.

Większość modułów ma poniższy układ. Fiesta_clock zachowuje starsze nazwy
`main.c` i `RTC.c`. VP37TestBench implementuje funkcje wejściowe HAL
w `logic.cpp`, bez CAN i sesji konfiguratora.

```text
src/<Module>/
├── start.{c,cpp}/.h       # start, tabela timerów, watchdog
├── device_tree.json       # płytka, urządzenia, piny i adresy
├── hardwareConfig.h       # stałe elektryczne i kontrola połączeń
├── hal_project_config.h   # włączone funkcje HAL
├── config.{c,cpp}/.h      # ustawienia i sesja konfiguratora
├── can.{c,cpp}/.h         # ramki CAN
├── <pliki logiki modułu>
├── CMakeLists.txt         # testy hostowe
├── tests/                 # testy kompilowane jako C++
└── .vscode/               # manifest i zadania jh-vscode
```

## Komunikacja

### CAN

| Magistrala | Uczestnicy | Cel |
|---|---|---|
| CAN0 „main" | ECU, Clocks, Fiesta_clock | Komunikacja między modułami. |
| CAN1 „OBD-2" | ECU, gniazdo OBD-II | Zewnętrzna diagnostyka. |

ECU publikuje na CAN0 stan silnika, RPM, przepustnicę, doładowanie, poziom
i temperaturę paliwa. Ramka `0x12B` oznacza temperaturę paliwa bitem ważności,
a bez niego wysyła 0.
Odbiera olej, prędkość i EGT z Clocks, stan DPF z `0x124` oraz czas RTC
z Fiesta_clock (`0x130`), którym oznacza nowe DTC. DTC są odczytywane przez CAN1 i nie trafiają na CAN0.
Clocks nadaje jasność ekranu (`0x126`), ciśnienie oleju z prędkością z ABS
(`0x12A`), obie temperatury spalin (`0x12F`), pozycję i czas GPS (`0x12D`,
`0x12E`) oraz UTC z GPS (`0x131`).
Fiesta_clock nadaje czas UTC (`0x130`) i temperatury powietrza (`0x132`)
oraz odbiera tylko UTC z GPS w Clocks. Na CAN1 ECU odpowiada na OBD-II i UDS. PID temperatur spalin i DPF oraz DID
ciśnienia oleju dostają odpowiedź negatywną (`0x22`, conditions not correct),
gdy Clocks oznacza tę wartość jako nieważną albo jego ramki przestają
przychodzić. Tak samo odpowiada DID temperatury paliwa `DD02`, gdy Adjustometer
zgłasza awarię czujnika albo przestaje odpowiadać. Jeśli każda dobra ramka
Adjustometera zgłasza awarię przez `ECU_FUEL_TEMP_FAULT_CONFIRM_MS`
(`src/ECU/config.h`), ECU zgłasza U190A; pierwsza ramka bez awarii zdejmuje
aktywny kod.
Bus-off lub odrzucone nadanie zgłasza U0073; `DTC_DETAIL_*` wskazuje ramkę,
a rekord UDS zawiera tę informację jako trzeci bajt DTC.

### I²C

ECU jest masterem dla Adjustometera (`0x57`) i ekspandera PCF8574 (`0x38`).
Clocks i Fiesta_clock mają osobne magistrale dla swoich czujników.

### USB

ECU, Clocks i Fiesta_clock występują jako urządzenia
`Fiesta <Module>` z UID płytki w numerze seryjnym. Konfigurator rozróżnia je
również przy podłączeniu kilku płytek. Format transmisji, tożsamość i sekwencję
wgrywania opisuje [protokół](src/common/scDefinitions/PROTOCOL.pl.md).

## Połączenia z samochodem

| Kierunek | Sygnały |
|---|---|
| Czujniki -> ECU | Temperatury płynu, oleju i dolotu; paliwo, przepustnica, ciśnienie dolotu, Hall RPM, przycisk ogrzewanej szyby, zasilanie. |
| Czujniki -> Clocks | Olej, ABS, EGT przed DPF i wewnątrz, GPS. |
| ECU -> samochód | VP37 (PWM i przekaźnik), N75, świece i kontrolka, wentylator, dwa poziomy grzania bloku, lewa/prawa część szyby, kontrolka DPF. |
| Clocks -> kierowca | Wskaźniki prędkości, obrotów i oleju (wejścia częstotliwościowe), kontrolki oleju i ładowania, TFT, buzzer. |
| Diagnostyka | OBD-II przez CAN1. |
| Pomocnicze | Pozycja i UTC z GPS w Clocks; czas RTC w UTC i temperatury powietrza z Fiesta_clock. |

## Zapis danych

Moduły zapisujące ustawienia używają KV JaszczurHAL na emulowanym EEPROM
(`ECU_EEPROM_SIZE_BYTES` w ECU). ECU przechowuje tam także DTC; muteks chroni
odczyty rdzenia 1 przed równoczesnym zapisem rdzenia 0. Fiesta_clock
przechowuje tam tylko znacznik, że jego RTC zawiera UTC.

## Kompilacja i CI

[Testy hostowe i CI](doc/TESTING.pl.md) opisują wybór modułów i kontrole.
Fiesta_clock ma buildy firmware i testy hostowe wyświetlacza oraz obsługi
czasu, a VP37TestBench lokalne buildy i testy hostowe, bez firmware CI.
SerialConfigurator ma osobny projekt testów aplikacji desktopowej.

[Instrukcja kompilacji](doc/BUILDING.pl.md) opisuje ścieżki montaży i wariantów,
manifesty, wgrywanie oraz SWD. `jh-vscode build` tworzy firmware; bootstrap
Fiesty dodaje i sprawdza manifest. [Runner systemd](src/ECU/scripts/systemd/README.pl.md)
może codziennie przygotowywać środowisko, testować i kompilować projekt oraz
wysyłać wynik e-mailem.

## Układ repozytorium

```text
Fiesta/
├── README.md                    # wprowadzenie
├── ARCHITECTURE.md              # architektura
├── MISRA.md                     # zakres i zasady MISRA-C
├── doc/                         # kompilacja, testy, galeria
├── modules.json                 # rejestr modułów
├── scripts/                     # rejestr, VS Code, duplikaty, hooki Git
├── .github/workflows/           # CI
├── src/
│   ├── ECU/ Clocks/ Fiesta_clock/ Adjustometer/ VP37TestBench/
│   ├── JaszczurHAL/             # submoduł HAL
│   ├── SerialConfigurator/      # aplikacja desktopowa i CLI
│   └── common/
│       ├── canDefinitions/      # identyfikatory i ramki CAN
│       ├── cmake/               # współdzielone helpery CMake
│       ├── scDefinitions/       # protokół konfiguratora
│       ├── scripts/             # manifesty, UF2 i nazwy modułów
│       ├── tests/               # helpery testów hostowych
│       └── vp37/                # sterownik VP37 i jego testy
├── Fiesta_pcbs/                 # schematy, PCB i złącza
├── materials/                   # dokumenty referencyjne i zdjęcia
└── legacy/                      # wycofane moduły
```

## Dalsze informacje

- Identyfikatory i ramki: [canDefinitions.h](src/common/canDefinitions/canDefinitions.h).
- Piny i adresy: `device_tree.json` modułu.
- Start i harmonogram: `start.{c,cpp}` modułu.
- Złącza i przewody: [pinout.txt](Fiesta_pcbs/pinout.txt), [wirings.txt](Fiesta_pcbs/wirings.txt).
- Wyniki MISRA: raport workflow `ecu-misra.yml` i [instrukcja runnera](src/ECU/misra/README.pl.md).
- Implementacja HAL: repozytorium JaszczurHAL.
