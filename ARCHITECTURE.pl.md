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
   │    ECU    │ ◄──────────  Fiesta_clock    czas RTC w UTC, 1 Hz
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

## Sprzęt

ECU pracuje na Waveshare RP2350-Plus. Jego PCB przyjmuje też RP2040-Plus
o tym samym układzie złączy; `device_tree.json` zawiera oba montaże
z identycznym okablowaniem, domyślny jest `rp2350-plus`. Pozostałe moduły
używają płytek RP2040: Raspberry Pi Pico i RP2040-Zero. Każdy moduł opisuje płytkę, urządzenia i piny w `device_tree.json`. Build generuje
z niego `jh_hardware.h`, a kod czyta makra `JH_HW_*`.

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
| [ECU](src/ECU/) | C | Silnik, diagnostyka i elementy wykonawcze. | Wymagany; VP37 ma bramkę bez aktywnych znalezisk, reszta raport analizy. |
| [Clocks](src/Clocks/) | C++ | Zestaw wskaźników; olej, prędkość, temperatury spalin i GPS. | Poza zakresem. |
| [Fiesta_clock](src/Fiesta_clock/) (`RTC_Clock`) | C | RTC ustawiany według UTC z GPS, czas przez CAN. | Poza zakresem. |
| [Adjustometer](src/Adjustometer/) | C | Pomiar położenia VP37, slave I²C. | Wymagany, bramka bez aktywnych znalezisk. |
| [VP37TestBench](src/VP37TestBench/) | C, C++ | Współdzielony napęd VP37 na stanowisku. | Bramka obejmuje współdzielony VP37, własne pliki stanowiska są poza zakresem. |
| [SerialConfigurator](src/SerialConfigurator/) | C, GTK4 | Konfiguracja i wgrywanie firmware. | Poza zakresem. |

[`modules.json`](modules.json) określa rodzaj, token, pozycję konfiguratora
i katalog testów każdego modułu. Dodawanie modułu zaczyna się od tego rejestru.
[`scripts/fiesta_modules.py`](scripts/fiesta_modules.py) sprawdza go i generuje
nagłówek tokenów w `src/common/scDefinitions`, tabelę konfiguratora oraz listy
dla skryptów powłoki. Korzystają z nich generator VS Code, `runalltests.sh`,
`runmefirst.sh`, bootstrap i konfigurator. Indeks pozycji konfiguratora jest
jawny, ponieważ wiąże zapisane ścieżki obrazów z modułem. Listy w workflow CI
są zapisane bezpośrednio, a test porównuje je z rejestrem.

[`legacy/`](legacy/) zawiera wycofane DPF_main, AdaptiveLights i Fading.
Nie są kompilowane ani testowane.

### ECU

ECU steruje pompą wtryskową VP37, zaworem doładowania N75, świecami żarowymi,
wentylatorem, grzałką bloku i ogrzewaną szybą. Przechowuje DTC i odpowiada na
OBD-II jako Ford Fiesta 1.8 DI EEC-V ECU.

Rdzeń 0 obsługuje timery, CAN, diagnostykę, przekaźniki, pamięć i sesje USB.
Rdzeń 1 wykonuje cykl VP37, przerwanie pomiaru RPM i sterowanie turbiną.
Nie ma RTOS. Rdzeń sterujący publikuje atomowe kopie stanu dla rdzenia 0;
dostęp do zatrzasku wyjść I²C i pamięci DTC chronią muteksy. Flash zapisuje rdzeń 0.

[Dokumentacja ECU](src/ECU/README.pl.md) opisuje synchronizację, zadawanie
położenia, sprzężenie prądowe, kompensację, telemetrię, RAM i obraz BENCH.
[Dokumentacja Adjustometera](src/Adjustometer/README.pl.md) opisuje pomiar położenia.

ECU współdzieli I²C 400 kHz między Adjustometer (`0x57`) i ekspander
przekaźników PCF8574A (`0x38`). PCF8574A jest przeznaczony do 100 kHz,
więc pracuje poza specyfikacją do czasu wymiany albo wydzielenia magistrali.
Lokalna definicja urządzenia ogranicza konfigurację magistrali do 400 kHz.

### Clocks

Clocks obsługuje zestaw wskaźników i mierzy to, czego nie odczytuje ECU.
Generuje przebiegi prostokątne dla fabrycznego prędkościomierza, obrotomierza
i wskaźnika oleju, steruje kontrolkami oleju i ładowania, rysuje dodatkowe
wskazania na TFT i obsługuje buzzer. Mierzy ciśnienie oleju czujnikiem
rezystancyjnym, prędkość przez impulsy ABS oraz temperatury spalin przed DPF
i wewnątrz niego przez dwa MCP9600 na własnym I²C.

Pracę dzielą dwa rdzenie RP2040. Rdzeń 0 odbiera i nadaje CAN, mierzy prędkość
z ABS oraz steruje wskaźnikami, kontrolkami i buzzerem. Rdzeń 1 rysuje TFT,
odczytuje czujnik oleju i obie termopary oraz obsługuje sesję konfiguratora.
Rdzenie wymieniają kopie swojego stanu pod krótką blokadą, więc rysowanie ani
powolny odczyt I²C nie opóźniają ramki CAN.

Clocks nadaje jasność ekranu, ciśnienie oleju i prędkość z ABS co 50 ms, a obie
temperatury spalin co 250 ms. Każdy pomiar ma flagę ważności. Brak termopary
albo zwarty lub przerwany czujnik oleju trafia na magistralę ze skasowaną
flagą, więc odbiorca nie odczyta go jako zera.

Odczyt EGT na TFT pokazuje temperaturę przed DPF. Co 5 s zmienia się
z temperaturą wewnątrz DPF tylko wtedy, gdy podłączony moduł DPF zgłasza
regenerację w ramce `0x124`.

Rdzeń 1 odczytuje GPS przez PIO/software serial: TX odbiornika łączy się
z GP27, a RX z GP28, przy 9600 baud. Clocks uruchamia się normalnie bez
odbiornika i fixa.

Współrzędne pozostają w `double` do przeliczenia na mikrostopnie dla
`SC_GET_GPS` i CAN. Lokalny GPS pozostaje dostępny po utracie komunikacji
z ECU. ECU wysyła dawne pola GPS ramki `0x129` jako zera, a Clocks ich
nie odczytuje. Odczyt działa w każdym
obiegu, a nowe dane i zmiany ważności wyzwalają publikację migawki.
Odświeżanie ekranu korzysta z przeliczonej wcześniej prędkości.

Ostatnia pozycja wygasa po 4 s. UTC pochodzi z jednego zdania RMC z fixem
i wygasa po 2 s. Clocks zachowuje UTC dla CAN i SC oraz nadaje go z sekundami
i ważnością w `0x131` co sekundę.

Piktogram GPS na TFT ma zieloną kropkę przy fixie, a bez niego czerwoną,
migającą raz na sekundę. Odbiornik nadaje NMEA także bez fixa. Gdy przez 3 s
nie przyjdzie żadne zdanie z poprawną sumą kontrolną, Clocks uznaje go za
nieobecny: zamiast prędkości pojawia się N/A, jak przy brakującej termoparze,
a kropka miga dwa razy szybciej.

Co sekundę Clocks wysyła też pozycję z prędkością GPS (`0x12D`) oraz pozycję
z datą i czasem UTC z dokładnością do minuty (`0x12E`). Jego device tree
odwołuje się do bindingu GPS w `src/common/bindings/`.

Kontrolka ładowania korzysta z obrotów silnika i napięcia akumulatora z ECU.
Świeci, gdy silnik stoi albo te dane są nieaktualne, i gaśnie, gdy po
rozruchu napięcie potwierdzi ładowanie. Zapala się ponownie po zaniku
ładowania i miga z częstotliwością 2 Hz przy zbyt wysokim napięciu. Progi
i czasy potwierdzeń są w [config.h](src/Clocks/config.h); wskaźnik napięcia
na TFT ma własny zakres.

| Plik | Zadanie |
|---|---|
| [Cluster.cpp](src/Clocks/Cluster.cpp) | Generuje sygnały fabrycznych wskaźników. |
| [Gauge.h](src/Clocks/Gauge.h), [simpleGauge.cpp](src/Clocks/simpleGauge.cpp), [tempGauge.cpp](src/Clocks/tempGauge.cpp), [pressureGauge.cpp](src/Clocks/pressureGauge.cpp) | Przeliczają wskazania. |
| [TFTExtension.cpp](src/Clocks/TFTExtension.cpp) | Rysuje ekran TFT. |
| [logic.cpp](src/Clocks/logic.cpp) | Uruchamia oba rdzenie i ich pętle; zamienia dane CAN na stan wskaźników, ekranu i buzzera. |
| [clocksState.cpp](src/Clocks/clocksState.cpp) | Przekazuje kopie stanu między rdzeniami. |
| [speed.cpp](src/Clocks/speed.cpp) | Przelicza częstotliwość impulsów ABS na prędkość. |
| [oilPressure.cpp](src/Clocks/oilPressure.cpp) | Przelicza ADC na bary i wykrywa zwarcie lub przerwę czujnika. |
| [egt.cpp](src/Clocks/egt.cpp) | Odczytuje oba MCP9600 i ponownie szuka brakującego układu. |
| [gps.cpp](src/Clocks/gps.cpp) | Odczytuje lokalny GPS, kontroluje świeżość UTC i koduje ramki GPS. |
| [chargeLamp.cpp](src/Clocks/chargeLamp.cpp) | Wyznacza stan kontrolki ładowania z obrotów i napięcia. |
| [buzzer.cpp](src/Clocks/buzzer.cpp), [buzzerStrategy.cpp](src/Clocks/buzzerStrategy.cpp) | Generują tony i ostrzeżenia. |
| [can.cpp](src/Clocks/can.cpp) | Odbiera CAN i wysyła ramki jasności, oleju z prędkością, EGT, pozycji GPS oraz UTC z GPS; filtry MCP2515 przepuszczają identyfikatory Fiesty. |

Kontroler CAN i TFT współdzielą SPI. Ekran zapisuje dane pasami o rozmiarze
`HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES` z
[hal_project_config.h](src/Clocks/hal_project_config.h), więc CAN dostaje
magistralę między dwoma pasami.

### Adjustometer

Adjustometer mierzy częstotliwość oscylatora Hartleya połączonego z cewkami
czujnika pompy, około 22-37 kHz. HAL zbiera bloki 32 okresów przez PIO i DMA.
Cztery bloki tworzą okno 128 okresów przesuwane o 32; surowe okna są uśredniane
przez okres napędu VP37, co usuwa tętnienia PWM bez opóźnienia dodatkowego filtra.
Publikowana odchyłka od poziomu odniesienia ma jednostkę Hz; kalibracja ECU
przelicza ją na położenie.

Rdzeń 0 odbiera pomiary i publikuje wynik. Rdzeń 1 obsługuje pomocnicze ADC,
diagnostykę, LED i logowanie USB. Moduł ma własny RP2040 i zwykle mieści się na PCB ECU.

Slave I²C `0x57` udostępnia:

| Rejestry | Zawartość |
|---|---|
| `0x00..0x04` | Starszy format impulsu, napięcie, temperatura paliwa i status. |
| `0x05..0x16` | Częstotliwość, poziom odniesienia, odchyłka ze znakiem i temperatura MCU. |
| `0x17..0x34` | Wersjonowany blok 30 B dla sterowania: numer próbki, czas i wiek pomiaru. |

Aktywny sterownik VP37 wymaga ostatniego bloku. ECU sprawdza spójność i świeżość;
starszy firmware z samym blokiem legacy nie dostarcza wymaganego sprzężenia.
HAL zamraża mapę na czas odczytu I²C. Bity statusu oznaczają brak sygnału
(`0x01`), uszkodzenie czujnika paliwa (`0x02`), oczekiwanie na poziom
odniesienia (`0x04`) i niewłaściwe zasilanie (`0x08`).

Start obejmuje 500 ms rozgrzewania, ustalenie poziomu odniesienia (minimum
80 ms, wymuszone po 250 ms) i 1000 ms kontroli dryftu. ECU czeka do
`VP37_ADJUSTOMETER_BASELINE_WAIT_MS` (8 s), zanim kalibruje skok. Szczegóły
pomiaru, startu i resetu opisuje [README](src/Adjustometer/README.pl.md).
Adjustometer nie obsługuje protokołu konfiguratora.

### VP37TestBench

VP37TestBench uruchamia `src/common/vp37` na stanowisku pompy: RP2040,
wyświetlacz ST7796S, stopień mocy tego samego typu co ECU i Adjustometer
z tym samym protokołem I²C. Elementy i wartości stopnia opisuje
[fiesta-vp37-power-stage.json](src/common/vp37/fiesta-vp37-power-stage.json),
a piny własny `device_tree.json`; odczytuje je
[vp37_power_stage.h](src/common/vp37/vp37_power_stage.h).

[vp37_bench_adapter.c](src/VP37TestBench/vp37_bench_adapter.c) obsługuje dwa
kanały PWM aktywne stanem niskim, włączenie napędu przez GPIO zamiast PCF8574
oraz I²C bez muteksu, bo magistrala ma jednego użytkownika. Zarówno stanowisko,
jak ECU przez [ecu_hal_config.h](src/ECU/ecu_hal_config.h), dołączają
[vp37_hal_config.h](src/common/vp37/vp37_hal_config.h). Każda faza I²C ma limit
2 ms, aby zablokowana magistrala nie przekroczyła granicy świeżości sprzężenia
20 ms. Brak limitu zatrzymuje kompilację.

Położenie zadaje potencjometr na pomocniczym wejściu skanu. Rdzeń 0 obsługuje
timery, ekran i logowanie; rdzeń 1 uruchamia pompę i `VP37_process()`.
Ekran odświeża wartości po zmianie. Kropka w rogu świeci stale na zielono,
gdy napływają ramki Adjustometera, i miga na czerwono przy ich braku.
Po zatrzymaniu pompy rdzeń 1 odpytuje status raz na sekundę. Bez pompy start
kończy się timeoutem poziomu odniesienia, a stanowisko nadal działa; pozwala
to sprawdzić podstawowe uruchomienie. Moduł nie obsługuje protokołu konfiguratora.

### Fiesta_clock

Fiesta_clock przechowuje w PCF8563 czas UTC i co sekundę nadaje go jako
`CAN_ID_RTC_UPDATE` (`0x130`). Integralność 1 ramka ma tylko przy zaufanym
UTC; w pozostałych przypadkach bajty daty i czasu są zerowe. Znacznik w KV
JaszczurHAL zapisuje, że ten firmware ustawił RTC na UTC. Do tego momentu RTC
zawiera czas lokalny zapisany przez starszy firmware: ekran pokazuje go bez
zmian, a ramka nie zgłasza integralności. Znacznik powstaje dopiero wtedy,
gdy odczyt RTC potwierdzi nowy czas UTC. Gdy odczyt kontrolny albo zapis
znacznika zawiedzie, a flash odpowie, że znacznika nie ma, zegar zapisuje
tę samą chwilę z powrotem jako czas lokalny, więc ekran i kolejny start
odczytują ją poprawnie. Gdy flash nie odpowiada, znacznik może tam być: RTC
zostaje przy UTC, wyświetlanym poprawnie, ale niezaufanym, a zegar co 30 s
próbuje potwierdzić znacznik. Start, przy którym flash jest nieczytelny,
pokazuje RTC jako UTC bez zaufania, bo większość zegarów ma już wtedy znacznik.

Zegar ustawia RTC według ramki UTC z GPS w Clocks (`0x131`). Filtry MCP2515
przepuszczają tylko ten identyfikator. RTC bez zaufanego UTC ustawia pierwsza
poprawna ramka, także gdy rejestry RTC nie zawierają poprawnej daty. Działający
RTC jest korygowany dopiero przy odchyłce większej niż 2 s i nie częściej niż
raz na 10 minut; po błędzie RTC, także nieudanym odczycie, albo błędzie flasha
kolejna próba następuje po 30 s. Każdy zapis jest sprawdzany odczytem. Ramki
odczytane po przerwie pętli głównej dłuższej niż 500 ms albo w czasie edycji
czasu przyciskami lub konfiguratorem są odrzucane. Bez GPS i CAN RTC pracuje
samodzielnie.

Ekran, przyciski i konfigurator używają czasu CET/CEST przeliczanego przez
`hal_time`. Ustawiony czas jest zamieniany na UTC. Godzina pominięta przy
wiosennej zmianie czasu liczy się jako CET, a godzina powtórzona jesienią
jako jej pierwsze, letnie wystąpienie. Dzień tygodnia wynika z daty, więc
przyciski go nie ustawiają. Konfigurator odczytuje i ustawia czas; commit
odrzuca niepoprawną datę, uwzględniając lata przestępne.

Lokalny ekran pokazuje czas, temperatury dwóch DS18B20 i napięcie zasilania. OLED SSD1306 128×32 obsługuje sterownik
JaszczurHAL. Ekran i RTC współdzielą I²C opisane w `device_tree.json`.
HAL zapisuje napisy bezpośrednio do pamięci panelu, bez bufora obrazu.
Zegar zachowuje pierwotny font 6×8 (w tym „ś"), skalę 1 i 2 oraz ustawienia
panelu, w tym kontrast 0x3f. Tryb bezczynności usypia panel, zachowując
pamięć kontrolera. Zmiana stanu zapłonu ponownie ustawia konfigurację panelu.
Po początkowym odczycie temperatur powitanie jest kasowane na ekranie.

### Serial Configurator

Aplikacja C/GTK4 dla Linuksa wykrywa moduły USB, pokazuje tożsamość,
odczytuje i zmienia parametry, uruchamia testy obrazu stanowiskowego ECU
oraz wgrywa firmware po sprawdzeniu manifestu. CLI obsługuje wykrywanie,
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

ECU publikuje na CAN0 stan silnika, RPM, przepustnicę, doładowanie i paliwo.
Odbiera olej, prędkość i EGT z Clocks, stan DPF z `0x124` oraz czas RTC
z Fiesta_clock (`0x130`), którym oznacza nowe DTC. DTC są odczytywane przez CAN1 i nie trafiają na CAN0.
Clocks nadaje jasność ekranu (`0x126`), ciśnienie oleju z prędkością z ABS
(`0x12A`), obie temperatury spalin (`0x12F`), pozycję i czas GPS (`0x12D`,
`0x12E`) oraz UTC z GPS (`0x131`).
Fiesta_clock nadaje czas UTC (`0x130`) i odbiera tylko UTC z GPS w Clocks. Na CAN1 ECU odpowiada na OBD-II i UDS. PID temperatur spalin i DPF oraz DID
ciśnienia oleju dostają odpowiedź negatywną (`0x22`, conditions not correct),
gdy Clocks oznacza tę wartość jako nieważną albo jego ramki przestają
przychodzić.
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
| Pomocnicze | Pozycja i UTC z GPS w Clocks; czas RTC w UTC z Fiesta_clock. |

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
