# VP37 Adjustometer

[English](README.md) · [Architektura](../../ARCHITECTURE.pl.md)

Czujnik położenia nastawnika pompy VP37, przekazujący do ECU pomiar oparty
na częstotliwości przez I2C.

## Zasilanie i kolejność startu

1. Sprawdź [połączenia płytki](../../Fiesta_pcbs/vp37_adjustometer/adjustometer/README.pl.md). Potrzebne są 5 V i 12 V ze wspólną masą; regulator 8 V zasila tylko oscylator. Odłącz zewnętrzne 5 V przed zasileniem RP2040-Zero przez USB.
2. Wyłącz napęd ECU i zaczekaj na zatrzymanie mechanizmu. Uruchom lub zresetuj Adjustometer.
3. Zaczekaj na ważny pomiar bazowy przy położeniu bliskim zera. Dopiero wtedy zresetuj ECU, które rozpocznie kalibrację skoku.

Otwarcie lub zamknięcie USB CDC może zresetować płytkę; wtedy zachowaj tę samą kolejność.
Brak sygnału zeruje publikowany pomiar, więc samo zero nie potwierdza poprawnego sprzężenia.
Sprawdzaj również status i świeżość.

## Odczyt LED

Utrata sygnału ma pierwszeństwo: LED miga czerwono z częstotliwością 4 Hz.
Bez błędów świeci zielono z połową jasności. Przy błędach co 500 ms przechodzi
przez aktywne warunki: fiolet dla temperatury paliwa, żółty dla napięcia,
czerwony po 2 sekundach bez transakcji I2C, a na końcu zielony heartbeat.

## Build firmware

Przygotuj środowisko według [instrukcji kompilacji](../../doc/BUILDING.pl.md), a następnie
otwórz folder `src/Adjustometer`. Zadanie `Project: Build` buduje firmware, a `Project:
Upload` sprawdza tożsamość modułu i wgrywa obraz. Dla płytki już w BOOTSEL wybierz zamiast
niego `Project: Upload (UF2 / BOOTSEL)`.

Przy zmianie częstotliwości PWM zbuduj zgodne obrazy ECU i Adjustometera. Manifest dla
SerialConfiguratora przygotuj osobno według instrukcji builda. Monitor portu używa USB CDC;
pamiętaj o kolejności resetów powyżej.

## Pomiar

Cewki czujnika pompy są elementem rezonansowym zmodyfikowanego generatora
Hartleya. Częstotliwość zmienia się w przybliżeniu w zakresie 22-37 kHz wraz
z położeniem nastawnika. Sprzętowy pomiar okresów w JaszczurHAL dostarcza bloki
32 pełnych okresów. Adjustometer składa cztery bloki w okno 128 okresów. RP2040 zapisuje
znaczniki czasu przez PIO i DMA, bez przerwania GPIO na każde zbocze. Wartość
`PULSE` jest modułem odchylenia filtrowanej częstotliwości od ustalonego zera,
z histerezą w pobliżu zera. Kalibracja położenia i kompensacja sterowania
należą do ECU.

Po ustaleniu poziomu odniesienia (baseline) okno przesuwa się co 32 okresy
(`ADJUSTOMETER_SLIDING_WINDOW`, domyślnie 1), a publikowana częstotliwość to średnia ważona
czasem z surowych okien z dokładnie jednego okresu PWM nastawnika
(`ADJUSTOMETER_PWM_FILTER_US`, domyślnie okres `VP37_PWM_FREQUENCY_HZ` z
`src/common/vp37_drive_config.h`, wspólnego z ECU). Uśrednienie po jednym okresie PWM znosi
tętnienie, wywołane przez napęd PWM, bez opóźnienia filtru rekurencyjnego: skok ustala się w
jednym okresie, a rampa spóźnia się o pół okresu, 2,5 ms przy domyślnych 200 Hz. Zmianę
częstotliwości PWM trzeba podać obu firmware'om. `ADJUSTOMETER_PWM_FILTER_US=0` przywraca
ułamkowy EMA przesuwanego okna (waga 71/1024, wyjście z zera po ośmiu krótkich
aktualizacjach). Ustalanie baseline zachowuje pierwotne tempo i całkowitoliczbowy EMA.
`ADJUSTOMETER_FEEDBACK_MIN_PUBLISH_MS` pozwala ustalić minimalny odstęp publikacji I2C;
domyślnie wynosi zero, a zmiany statusu są publikowane od razu. Snapshot w HAL zapewnia
spójność odczytu I2C podczas publikacji nowych pomiarów.

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

Podtrzymanie zera (zero hold) włącza się w granicach 40 Hz, a zwalnia po przekroczeniu 50 Hz przez
dwa kolejne okna o tym samym znaku. Utrata sygnału jest wykrywana po trzech
okresach filtrowanej częstotliwości od ostatniego pełnego bloku pomiarowego,
z ograniczeniem do 10-200 ms. Błąd przechwytywania okresów odrzuca niepełne okno i unieważnia
pomiar; po przepełnieniu lub błędzie peryferium pomiar uruchamia się ponownie
po 100 ms. Ustalony baseline zostaje zachowany; przerwana weryfikacja
podczas startu rozpoczyna się ponownie. Przy utracie sygnału pulse
wynosi zero i ustawiany jest `SIGNAL_LOST`; odbiorca musi sprawdzać status
oraz aktualność pomiaru.

## I2C i czujniki pomocnicze

Adres slave to `0x57`; ECU używa 400 kHz. Bloki feedbacku i diagnostyki są
publikowane atomowo. `HAL_ENABLE_I2C_SLAVE_SNAPSHOT` utrwala mapę na czas
odczytu; znaczniki sekwencji i kontrola świeżości w ECU pozostają aktywne. Wspólne definicje znajdują się w
[adjustometer_protocol.h](../common/adjustometer_protocol.h).

| Rejestry | Przeznaczenie |
| --- | --- |
| `0x00..0x04` | Zarezerwowane; bez danych pomiarowych. |
| `0x05..0x12` | Spójna diagnostyka w wersji 2: częstotliwość po filtrze, baseline, temperatura RP2040 i flagi poprawności. |
| `0x13..0x16` | Zarezerwowane. |
| `0x17..0x34` | Spójne dane regulatora, w tym surowa częstotliwość i aktualność pomiaru. |

ECU i VP37TestBench przyjmują ramki sterowania w wersjach 1 i 2 pod `0x17`.
Adjustometer wysyła wersję 2. Najpierw zaktualizuj ECU lub VP37TestBench:
starszy odbiornik odrzuca nową ramkę. Wersja 2 zachowuje układ 30 bajtów,
a w bajcie 5 zapisuje temperaturę w stopniach C plus 40 (`-20 C` daje `20`).
Wersja 1 przechowuje stopnie C bez znaku. Pięciobajtowy odczyt nie jest już
obsługiwany. Czytnik diagnostyki
wymaga wersji 2 i oblicza odchylenie ze znakiem jako częstotliwość po filtrze
minus baseline z tego samego odczytu. Odrzucenie innej wersji diagnostyki
nie wpływa na odczyt ramki sterowania.

Czytnik przyznaje próbce 20 ms ważności, uwzględniając zgłoszony wiek przy
publikacji, cały odczyt I2C i dalszy upływ czasu ECU. Kolejne odczyty tej samej
próbki nie odnawiają limitu; późniejsza publikacja z większym wiekiem może go
skrócić. Pole `ageUs` nadal podaje wiek przy publikacji. Niezsynchronizowane
zegary nie pozwalają ustalić opóźnienia między publikacją a pierwszym odczytem,
więc ta kontrola nie mierzy całego czasu od pomiaru do użycia danych.

Pomiar temperatury paliwa obejmuje od -40 do 120 C, również zero. Bit
`FUEL_TEMP_BROKEN` natychmiast oznacza błąd odczytu lub wartość poza zakresem.
Pole liczbowe zachowuje ostatnią poprawną temperaturę (zero przed pierwszą
poprawną próbką), więc zawsze sprawdzaj status. Po odzyskaniu pomiaru filtr
zaczyna od pierwszej poprawnej próbki. Zakres pomiaru nie potwierdza działania
regulatora na zimnej pompie. Przy 0 C i poniżej ECU zachowuje ostatni mnożnik
kompensacji, po starcie równy 1, oraz niższy limit dodatniej korekty regulatora.

Napięcie jest kodowane w 0,1 V.
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

## Podział pracy między rdzenie

Core 0 inicjalizuje czujniki, odbiera dane pomiarowe co 1 ms i publikuje szybkie
dane. Core 1 odczytuje ADC co 10 ms, publikuje
rozszerzenie diagnostyczne, obsługuje LED i USB. Temperaturę układu i logi
odświeża co 250 ms. Zapis USB ma zerowy timeout i może pomijać tekst, jeśli host
nie odbiera danych; publikacja pomiaru działa niezależnie.

## Kod i testy

| Pliki | Odpowiedzialność |
| --- | --- |
| `sensors.c / sensors.h` | Częstotliwość, EMA, baseline, zero hold, ADC i spójny pomiar w pamięci. |
| `telemetry.c / telemetry.h` | Publikacja spójnych bloków pomiaru i diagnostyki I2C. |
| `start.c / start.h` | Przenośny start aplikacji i praca rdzeni. |
| `led.c / led.h` | Sygnalizacja statusu. |
| `device_tree.json` | Płytka, piny, magistrala ECU i dioda statusu. |
| `config.h`, `hardwareConfig.h` | Czasy, progi i parametry analogowe. |
| `../common/adjustometer_feedback.h` | Wspólna reprezentacja pomiaru i kodowanie protokołu. |

Testy hostowe sprawdzają pomiar, dryf baseline, zero hold, utratę sygnału, LED
i przeplatanie odczytów z publikacją rejestrów. Testy ECU obejmują dekodowanie,
aktualność próbek, zawijanie czasu, ponowienia i wyłączenie sterowania.

```bash
cmake -S src/Adjustometer -B src/Adjustometer/build_test
cmake --build src/Adjustometer/build_test --parallel
ctest --test-dir src/Adjustometer/build_test --output-on-failure
```

## Licencja

Copyright (c) 2026 Marcin Jaszczur Kielesiński (jaszczurtd), jaszczurtd(at)tlen.pl

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software, hardware designs, and associated documentation files (the "Project"), to deal in
the Project without restriction, including without limitation the rights to use, copy,
modify, merge, publish, distribute, sublicense, and/or sell copies of the Project, subject to
the following conditions:

**Attribution requirement:** All copies, modified versions, and redistributions of the
Project - in whole or in part - must prominently include the following attribution in all
source files, documentation, and any accompanying materials:

> Original author: **Marcin Jaszczur Kielesiński** (jaszczurtd), jaszczurtd(at)tlen.pl

This attribution must not be removed, obscured, or altered in any way.

THE PROJECT IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE PROJECT OR THE USE OR OTHER DEALINGS IN THE PROJECT.
