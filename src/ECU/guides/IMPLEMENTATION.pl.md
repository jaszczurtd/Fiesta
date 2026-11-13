# Implementacja ECU

[English](IMPLEMENTATION.md) · [Uruchomienie ECU](../README.pl.md)

## Wykonanie i synchronizacja

- **Rdzeń 0** wykonuje pętlę główną: timery programowe, CAN, OBD-II,
  przekaźniki, zapis DTC i sesję konfiguratora.
- **Rdzeń 1** obsługuje sterowanie VP37, przerwanie RPM i turbinę.

Nie ma RTOS. `start.c` instaluje tabelę par `(period, callback)`, wykonywanych
przez rdzeń 0 od około 10 ms dla szybkich odczytów do sekundy dla obsługi
okresowej. VP37 należy do rdzenia 1. Kroki sterowania biegną po stałej
siatce okresów `pidTimeUpdate`: spóźniony start nie przesuwa następnego
terminu, a start spóźniony o cały okres zaczyna nową siatkę zamiast serii
kroków nadrabiających. W oknie `CORE1_VP37_GUARD_US` przed terminem rdzeń 1
nie zaczyna innej pracy (turbina, RPM, zadanie testu) i czeka na termin
z RAM, aby pozostała praca nie opóźniła kroku. Po osiągnięciu terminu
rdzeń obsługuje także zadanie i polecenia zatrzymania. Dzięki temu przeciążona
pętla pozostaje dostępna, gdy zadany okres jest krótszy od wykonywanej pracy;
nie oznacza to osiągnięcia tego okresu. Testy stanowiskowe zapisują
ogłoszenia kroków na rdzeniu 1, a drukuje je rdzeń 0, razem z potwierdzeniami
resetu nastaw. Zmienione zadanie testu czeka na swoje ogłoszenie, najwyżej `STEP_EVENT_APPLY_TIMEOUT_MS`, więc w logu
ogłoszenie nadal poprzedza ruch, który zapowiada. Każdy krok publikuje
stan za licznikiem sekwencji; rdzeń 0 kopiuje go bez blokady. Słowa danych są zapisywane
i odczytywane atomowo z porządkiem relaxed, więc kopia nakładająca się na
publikację zostaje odrzucona bez wyścigu danych. Sterowanie nie czeka na
telemetrię ani aktualizację wartości globalnych.

Sterownik PWM nie potrafi wytworzyć dokładnie okresu siatki sterowania, więc
swobodna siatka przesuwałaby się przez cały okres PWM (około 40 µs na sekundę
przy 200 Hz), a komenda czekałaby na zatrzask od zera do 5 ms. Siatka podąża
więc za zatrzaskiem mierzonym w skanie prądu: każdy krok przesuwa następny
termin najwyżej o `VP37_LATCH_LOCK_STEP_US`, tak aby zapis PWM wypadał
`VP37_LATCH_LOCK_LEAD_US` przed zatrzaskiem. Blokada uczy się też różnicy
okresów i stosuje ją dalej, gdy nastawnik jest zwolniony i zatrzasku nie widać.
Działa tylko wtedy, gdy okres sterowania równa się okresowi PWM; przy innym
okresie stanowiskowym albo wyłączonym skanie prądu siatka działa bez synchronizacji z zatrzaskiem PWM.

Telemetria stanowiskowa czeka na rdzeniu 0 na zakończenie kroku przed
formatowaniem raportu: pobieranie kodu z flash na tym rdzeniu wydłużało krok
średnio o około 300 µs. Bufor śladu zmienia właściciela przez jeden atomowy
stan. Magistralę I²C z zatrzaskiem PCF8574 i menedżer DTC z pamięcią chronią
muteksy. Flash zapisuje rdzeń 0.

Główne sterowniki VP37, turbo, RPM, wentylatora, grzałki, świec, szyb i stanu
silnika znajdują się w `ecu_context_t` w [ecuContext.h](../ecuContext.h).
CAN, czujniki, DTC, czas RTC, OBD i sesja konfiguratora mają stan lokalny w plikach.

## Obroty silnika

Wejście wału dostarcza 32 równomiernie rozmieszczone zbocza narastające na
obrót. ISR zapisuje znaczniki czasu w mikrosekundach. Odrzuca okresy krótsze
niż 94 µs (granica pomiaru 20000 rpm), nie przesuwając ostatniego przyjętego
zbocza. Ta granica jest oddzielna od progu ochrony silnika 5000 rpm.

Na rdzeniu 1 `RPM_process()` na krótko maskuje lokalne przerwania, aby
skopiować dziewięć znaczników czasu. Oblicza RPM z ostatnich od jednego do
ośmiu okresów mieszczących się w 25 ms. Gdy pojedynczy okres jest dłuższy,
używa go samodzielnie. Pierwsze zbocze rozpoczyna pomiar, a drugie daje
wynik. Zaokrąglenie całkowitoliczbowe nie ma dodanej korekcji, a wynik nie
jest przycinany do limitu silnika, więc blokada VP37 wykrywa nadobroty.
Rdzeń 0 odczytuje opublikowane RPM atomowo.

Sygnał wygasa po trzykrotności ostatniego przyjętego okresu, ograniczonej do
20-500 ms. Po pierwszym zboczu jest 500 ms na odebranie drugiego. Wygaśnięcie
zeruje wynik i odrzuca historię; ponowny pomiar wymaga dwóch nowych zboczy.
Różnice czasu obsługują zawinięcie 32-bitowego licznika mikrosekund.
To pomiar prędkości na równomiernym kole, bez dekodowania fazy wału lub
brakujących zębów.

## Budżet RAM sterowania

ECU zachowuje prąd i napięcie w pierścieniu dwóch kanałów; multiplekser
czujników pozostaje częścią skanu DMA z trzema kanałami. Redukcja prądu
oraz śledzenie zboczy używają `HAL_RAM_FUNC`, ograniczając pobieranie kodu
z flash. Ślad stanowiskowy mieści 1024 pełne próbki z flagami zapisanymi
jako bity. Długość określa `VP37_TRACE_SAMPLES`. Po zmianie buforów lub kodu
w RAM sprawdź mapy pamięci ELF obu wariantów firmware.

## Mapa źródeł

| Plik | Zadanie |
|---|---|
| [start.c](../start.c) | Kolejność startu, timery, watchdog i pętle rdzeni. |
| [sensors.c](../sensors.c) | Wejścia analogowe, filtrowane żądanie kierowcy, PCF8574 i odczyty I²C. |
| [can.c](../can.c) | Główne ramki CAN, w tym publikacja RPM. |
| [obd-2.c](../obd-2.c) | Wejście OBD CAN i odpowiedzi ISO-TP. |
| [obd_j1979.c](../obd_j1979.c) | Usługi SAE J1979 i PID Mode 01. |
| [obd_ford_diag.c](../obd_ford_diag.c) | Ford EEC-V UDS, KWP2000 i SCP. |
| [dtcManager.c](../dtcManager.c) | Zapis i odczyt diagnostyczny DTC. Rejestrowane kody to aliasy z [dtc_codes.h](../dtc_codes.h), a ich opisy pochodzą ze wspólnego katalogu. |
| [common/obdDefinitions](../../common/obdDefinitions/) | Katalog DTC współdzielony przez moduły: ogólne kody SAE J2012 i kody projektu U19xx z opisami, wyszukiwanie opisu i postać drukowana (`P0116`). Listę [obdDefinitions_sources.txt](../../common/obdDefinitions/obdDefinitions_sources.txt) buildy czytają tak jak listę VP37 niżej. |
| [rpm.c](../rpm.c) | Obroty z przerwania czujnika Halla. |
| [common/vp37](../../common/vp37/) | Sterownik współdzielony przez ECU i VP37TestBench. Listę [vp37_sources.txt](../../common/vp37/vp37_sources.txt) czyta CMake testów, a generator VS Code wpisuje ją do manifestów modułów z `sharedSources`. |
| [vp37.c](../../common/vp37/vp37.c) | Start pompy, API żądania i cykl sterowania. |
| [vp37_adapter.c](../vp37_adapter.c) | PWM ECU, enable PCF8574, blokada RPM, I²C Adjustometera, start, wartości `F_FUEL_TEMP`/`F_VOLTS` ze stanu publikowanego, ważność temperatury paliwa dla CAN i OBD oraz U190A po trwałej awarii czujnika paliwa. |
| [vp37_adjustometer.c](../../common/vp37/vp37_adjustometer.c) | Ramki, diagnostyka i oczekiwanie na poziom odniesienia. |
| [vp37_snapshot.c](../../common/vp37/vp37_snapshot.c) | Publikowanie i kopiowanie stanu bez blokady. |
| [vp37_feedback.c](../../common/vp37/vp37_feedback.c) | Położenie i kalibracja skoku. |
| [vp37_compensation.c](../../common/vp37/vp37_compensation.c) | Napięcie, temperatura paliwa i rezystancja cewki. |
| [vp37_control.c](../../common/vp37/vp37_control.c) | Mapa podtrzymania, wyuczona korekta, PID, martwa strefa i postój. |
| [vp37_current.c](../../common/vp37/vp37_current.c) | Pomiar prądu na boczniku. |
| [vp37_current_control.c](../../common/vp37/vp37_current_control.c) | Sprzężenie ON i historia PWM. |
| [vp37_tuning.c](../../common/vp37/vp37_tuning.c) | Sprawdzane zakresy nastaw stanowiska. |
| [vp37_telemetry.c](../../common/vp37/vp37_telemetry.c) | Próbki sterowania i ślady na rdzeniu 0, tylko obraz stanowiskowy. |
| [turbo.c](../turbo.c), [engineMaps.c](../engineMaps.c) | Sterowanie doładowaniem i tabela N75. |
| [vp37_maps.c](../../common/vp37/vp37_maps.c) | Mapa podtrzymania i zmiany całki oraz martwej strefy. |
| [engineFan.c](../engineFan.c), [engineHeater.c](../engineHeater.c), [glowPlugs.c](../glowPlugs.c), [heatedWindshield.c](../heatedWindshield.c) | Wyjścia przekaźnikowe. |
| [engineFuel.c](../engineFuel.c) | Poziom paliwa. |
| [rtcTime.c](../rtcTime.c) | Czas UTC z ramki `0x130` Fiesta_clock. Oznacza pierwsze wystąpienie DTC i raport restartu przez watchdog; po 3 s bez zaufanej ramki czas jest nieznany. |
| [config.c](../config.c) | Ustawienia i sesja konfiguratora. |
| [tests.c](../tests.c) | Rejestr testów, sterowane elementy, dostępność sekwencji i konfiguratora. |
| [tests_workers.c](../tests_workers.c) | Generatory żądań, czynności jednorazowe i postęp. |
| [tests_helpers.c](../tests_helpers.c) | Parametry ECU, komendy VP37, zadawanie demand i blokada RPM dla [wspólnego runnera (EN)](../../common/test_runner/README.md); bezczynne funkcje bez `ECU_FUNCTIONAL_TESTS_ENABLED`. |
