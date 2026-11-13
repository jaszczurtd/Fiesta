# Fiesta_clock

[English](README.md) · [Architektura](../../ARCHITECTURE.pl.md)

Moduł pokazuje czas, temperaturę wewnątrz i na zewnątrz oraz napięcie. Wysyła czas i
temperatury do pozostałych modułów przez CAN. RTC podtrzymuje czas także bez GPS.

## Przygotowanie i uruchomienie

Sprawdź [wersję płytki](../../Fiesta_pcbs/clock/README.pl.md) i typ RTC. Firmware domyślnie
wybiera PCF8563, a nowa płytka ma DS3231 Mini i zmieniony dzielnik napięcia. Wybierz wariant
DS3231 i dostosuj przeliczenie napięcia przed oceną wskazań. Zasilaj moduł z USB albo
samochodu, nigdy jednocześnie.

Otwórz `src/Fiesta_clock` i wykonaj [build oraz upload](../../doc/BUILDING.pl.md). Czas
ustawisz przyciskami lub przez [SerialConfigurator](../SerialConfigurator/README.pl.md).
Sprawdź datę, godzinę i oba czujniki; brak czujnika nie oznacza temperatury 0°C.

## Czas lokalny i odczyty temperatur

Ekran, przyciski i konfigurator używają czasu CET/CEST przeliczanego przez
`hal_time`. Ustawiony czas jest zamieniany na UTC. Godzina pominięta przy
wiosennej zmianie czasu liczy się jako CET, a godzina powtórzona jesienią
jako jej pierwsze, letnie wystąpienie. Dzień tygodnia wynika z daty, więc
przyciski go nie ustawiają. Konfigurator odczytuje i ustawia czas; commit
odrzuca niepoprawną datę, uwzględniając lata przestępne.

Zegar nadaje też temperatury powietrza wewnątrz i na zewnątrz z dwóch
czujników DS18B20 jako `CAN_ID_AIR_TEMP_UPDATE` (`0x132`), w 0,1 °C. Ramka
wychodzi razem z czasem co sekundę oraz od razu po zmianie odczytu lub jego
ważności. Oba czujniki mierzą w każdym trybie ekranu; pomiar 12-bitowy trwa
750 ms. Czujnik, którego najnowszy odczyt ma 3 s lub więcej, traci bit
ważności i nadaje 0. Czujnik nieobecny przy otwarciu pozostaje bez odczytu do
kolejnego otwarcia, które wykonują ekran temperatur i zmiana stanu zapłonu.

## Synchronizacja z GPS

Zegar ustawia RTC według ramki UTC z GPS w Clocks (`0x131`). Filtry MCP2515
przepuszczają tylko ten identyfikator. RTC bez zaufanego UTC ustawia pierwsza
poprawna ramka, także gdy rejestry RTC nie zawierają poprawnej daty. Działający
RTC jest korygowany dopiero przy odchyłce większej niż 2 s i nie częściej niż
raz na 10 minut; po błędzie RTC, także nieudanym odczycie, albo błędzie flasha
kolejna próba następuje po 30 s. Każdy zapis jest sprawdzany odczytem. Ramki
odczytane po przerwie pętli głównej dłuższej niż 500 ms albo w czasie edycji
czasu przyciskami lub konfiguratorem są odrzucane. Bez GPS i CAN RTC pracuje
samodzielnie.

## Zapis UTC i odzyskiwanie po błędach

Fiesta_clock przechowuje w PCF8563 lub DS3231 czas UTC i co sekundę nadaje go jako
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

## Implementacja wyświetlacza

Lokalny ekran pokazuje czas, temperatury dwóch DS18B20 i napięcie zasilania. OLED SSD1306 128×32 obsługuje sterownik
JaszczurHAL. Ekran i RTC współdzielą I²C opisane w `device_tree.json`.
HAL zapisuje napisy bezpośrednio do pamięci panelu, bez bufora obrazu.
Zegar zachowuje pierwotny font 6×8 (w tym „ś"), skalę 1 i 2 oraz ustawienia
panelu, w tym kontrast 0x3f. Tryb bezczynności usypia panel, zachowując
pamięć kontrolera. Zmiana stanu zapłonu ponownie ustawia konfigurację panelu.
Po początkowym odczycie temperatur powitanie jest kasowane na ekranie.
