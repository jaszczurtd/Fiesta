# VP37 Adjustometer

Czujnik położenia nastawnika pompy VP37, przekazujący do ECU pomiar oparty
na częstotliwości przez I2C.

## Pomiar

Cewki czujnika pompy są elementem rezonansowym zmodyfikowanego generatora
Hartleya. Częstotliwość zmienia się w przybliżeniu w zakresie 22-37 kHz wraz
z położeniem nastawnika. Sprzętowy pomiar okresów w JaszczurHAL dostarcza bloki
32 pełnych okresów. Adjustometer składa cztery bloki w okno 128 okresów i stosuje
całkowitoliczbowy filtr EMA. RP2040 zapisuje znaczniki czasu przez PIO i DMA,
bez przerwania GPIO na każde zbocze. Wartość `PULSE` jest
modułem odchylenia filtrowanej częstotliwości od ustalonego zera, z histerezą
w pobliżu zera. Kalibracja położenia i kompensacja sterowania należą do ECU.

Eksperymentalna definicja kompilacji `ADJUSTOMETER_SLIDING_WINDOW=1` zachowuje
128 okresów w pomiarze i odświeża wynik co 32 okresy po ustaleniu baseline.
EMA przechowuje ułamki herca i używa wagi 71/1024, aby przybliżyć dotychczasową
stałą czasową; wyjście z zera wymaga ośmiu krótkich aktualizacji. Ustalanie
baseline zachowuje pierwotne tempo. Domyślnie okna nadal są rozłączne.
`ADJUSTOMETER_FEEDBACK_MIN_PUBLISH_MS` pozwala ustalić minimalny odstęp
publikacji I2C; domyślnie wynosi zero, a zmiany statusu są publikowane od razu.
Snapshot w HAL zapewnia spójność odczytu I2C podczas publikacji nowych pomiarów.

Układ wykorzystuje ideę pomiaru rezonansowego znaną z VP37/EDC15, z zewnętrznym
RP2040 i cyfrowym I2C. Nie odwzorowuje elektroniki OEM ani nie podaje dawki
w mg/skok. Temperatura paliwa i napięcie są raportowane osobno; nie zmieniają
częstotliwości ani baseline. PCB i schemat są w `Fiesta_pcbs/vp37_adjustometer/`.

Bieżący ECU wymaga
30-bajtowego bloku `0x17..0x34`. Oba moduły muszą go obsługiwać.

## Start i poprawność sygnału

Przed włączeniem pomiaru program czeka `ADJUSTOMETER_WARMUP_MS` (500 ms).
Ustalanie baseline trwa co najmniej 80 ms, dopuszcza wymuszony lock po 250 ms
i wymaga sześciu stabilnych okien w granicach 12 Hz. Dodatkowa weryfikacja trwa
1000 ms; dryf powyżej 500 Hz ponawia ustalanie zera. ECU może kalibrować zakres
dopiero po uzyskaniu gotowości czujnika.

Zero hold włącza się w granicach 40 Hz, a zwalnia po przekroczeniu 50 Hz przez
dwa kolejne okna o tym samym znaku. Utrata sygnału jest wykrywana po trzech
okresach filtrowanej częstotliwości od ostatniego pełnego bloku capture,
z ograniczeniem do 10-200 ms. Błąd capture odrzuca niepełne okno i unieważnia
pomiar; po przepełnieniu lub błędzie peryferium pomiar uruchamia się ponownie
po 100 ms. Ustalony baseline zostaje zachowany; przerwana weryfikacja
podczas startu rozpoczyna się ponownie. Przy utracie sygnału pulse
wynosi zero i ustawiany jest `SIGNAL_LOST`; odbiorca musi sprawdzać status
oraz aktualność pomiaru.

Resetuj Adjustometer po wyłączeniu napędu ECU i ustaleniu pozycji spoczynkowej.
Poczekaj na poprawny baseline i odchylenie bliskie zeru, następnie zresetuj ECU.
Otwarcie lub zamknięcie USB CDC może resetować urządzenie; dla Adjustometera
obowiązuje wtedy ta sama kolejność.

## I2C i czujniki pomocnicze

Adres slave to `0x57`; ECU używa 400 kHz. Bloki feedbacku i diagnostyki są
publikowane atomowo. `HAL_ENABLE_I2C_SLAVE_SNAPSHOT` utrwala mapę na czas
odczytu; znaczniki sekwencji i kontrola świeżości w ECU pozostają aktywne. Wspólne definicje znajdują się w
[adjustometer_protocol.h](../common/adjustometer_protocol.h).

| Rejestry | Przeznaczenie |
| --- | --- |
| `0x00..0x04` | Pierwotny układ: pulse (signed int16, big-endian), napięcie, temperatura paliwa i status. Zachowany dla starszych odbiorców. |
| `0x05..0x16` | Opcjonalna spójna diagnostyka: częstotliwość po filtrze, baseline, odchylenie ze znakiem, temperatura RP2040 i flagi poprawności. |
| `0x17..0x34` | Spójne dane regulatora, w tym surowa częstotliwość i aktualność pomiaru. |

Napięcie jest kodowane w 0,1 V, temperatura paliwa w całych stopniach C.
Dzielnik `47k / 10k` ogranicza pomiar ADC do około 18,8 V przy zakresie 3,3 V,
choć pole protokołu mieści 25,5 V. Osobny EMA dla ADC nadaje nowej próbce wagę 1/8.

| Maska statusu | Znaczenie |
| --- | --- |
| `0x01` | `SIGNAL_LOST`: brak zboczy generatora. |
| `0x02` | `FUEL_TEMP_BROKEN`: niewiarygodny odczyt NTC. |
| `0x04` | `BASELINE_PENDING`: ustalanie lub weryfikacja zera trwa. |
| `0x08` | `VOLTAGE_BAD`: napięcie poza 8-15 V. |

Bity mogą występować razem. Bity 0, 1 i 2 diagnostycznego `EXT_FLAGS` oznaczają
poprawny sygnał, baseline/odchylenie ze znakiem i temperaturę układu. W logu ECU
`VP37 ADJ` zapis `ext:1 fl:0x07` oznacza udany spójny odczyt rozszerzenia
z trzema poprawnymi pomiarami. Ta próbka jest niezależna od odczytu regulatora.

## Podział rdzeni i LED

Core 0 inicjalizuje czujniki, odbiera dane capture co 1 ms i publikuje szybkie dane
oraz ich kopię w pierwotnych rejestrach. Core 1 odczytuje ADC co 10 ms, publikuje
rozszerzenie diagnostyczne, obsługuje LED i USB. Temperaturę układu i logi
odświeża co 250 ms. Zapis USB ma zerowy timeout i może pomijać tekst, jeśli host
nie odbiera danych; publikacja pomiaru działa niezależnie.

Utrata sygnału ma pierwszeństwo: LED miga czerwono z częstotliwością 4 Hz.
Bez błędów świeci zielono z połową jasności. Przy błędach co 500 ms przechodzi
przez aktywne warunki: fiolet dla temperatury paliwa, żółty dla napięcia,
czerwony po 2 sekundach bez transakcji I2C, a na końcu zielony heartbeat.

## Kod i testy

| Pliki | Odpowiedzialność |
| --- | --- |
| `sensors.c / sensors.h` | Częstotliwość, EMA, baseline, zero hold, ADC i spójny pomiar w pamięci. |
| `telemetry.c / telemetry.h` | Publikacja spójnych bloków I2C i pierwotnych rejestrów. |
| `start.c / start.h` | Przenośny start aplikacji i praca rdzeni. |
| `led.c / led.h` | Sygnalizacja statusu. |
| `config.h`, `hardwareConfig.h` | Czasy, progi, piny i parametry analogowe. |
| `../common/adjustometer_feedback.h` | Wspólna reprezentacja pomiaru i kodowanie protokołu. |

Testy hostowe sprawdzają pomiar, dryf baseline, zero hold, utratę sygnału, LED
i przeplatanie odczytów z publikacją rejestrów. Testy ECU obejmują dekodowanie,
aktualność próbek, zawijanie czasu, ponowienia i wyłączenie sterowania.

```bash
cmake -S src/Adjustometer -B src/Adjustometer/build_test
cmake --build src/Adjustometer/build_test --parallel
ctest --test-dir src/Adjustometer/build_test --output-on-failure
```

## Build firmware

Przygotuj toolchain Arm i przypięty Pico SDK przez `runmefirst.sh`.
JaszczurHAL powinien być w `<parent-of-Fiesta>/libraries/JaszczurHAL`.

```bash
cd src/Adjustometer
JH=../../../libraries/JaszczurHAL/vscode/entry/jh-vscode
"$JH" build --project "$PWD"
"$JH" build-debug --project "$PWD"
"$JH" upload --project "$PWD"
"$JH" upload-uf2 --project "$PWD"
"$JH" refresh-intellisense --project "$PWD"
```

`upload` odpowiada zadaniu VS Code (`Ctrl+Shift+2`), a `upload-uf2` używa BOOTSEL.
`monitor` otwiera USB CDC (`Ctrl+Shift+3`). Wybór portu (`Ctrl+Shift+9`) zmienia
lokalne ustawienia; upload nadal sprawdza tożsamość modułu. Obsługa manifestu
Fiesta i tożsamości USB znajduje się w `src/common/scripts/`.

## Licencja

Copyright (c) 2026 Marcin Jaszczur Kielesiński (jaszczurtd), jaszczurtd(at)tlen.pl

Permission is hereby granted, free of charge, to any person obtaining a copy of this software, hardware designs, and associated documentation files (the "Project"), to deal in the Project without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Project, subject to the following conditions:

**Attribution requirement:** All copies, modified versions, and redistributions of the Project - in whole or in part - must prominently include the following attribution in all source files, documentation, and any accompanying materials:

> Original author: **Marcin Jaszczur Kielesiński** (jaszczurtd), jaszczurtd(at)tlen.pl

This attribution must not be removed, obscured, or altered in any way.

THE PROJECT IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE PROJECT OR THE USE OR OTHER DEALINGS IN THE PROJECT.
