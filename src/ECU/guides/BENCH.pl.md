# Praca z ECU na stanowisku

[English](BENCH.md) · [Uruchomienie ECU](../README.pl.md)

Przed użyciem poleceń wykonaj [przygotowanie i uruchomienie](../README.pl.md). Ten dokument
zbiera wariant firmware, zasady sesji testowej, interfejsy sprzętowe i zapis danych.

## Polecenia według zadania

Polecenia wysyła konsola właściciela portu. Nie otwieraj dodatkowego monitora obok działającego przechwytywania.

| Zadanie | Polecenie | Wynik / warunek |
| --- | --- | --- |
| Odczytać dostępne testy i parametry | `list`, `params` | Odczyt bez uruchamiania testu. |
| Włączyć nadzór sesji konsoli | `watch <ms>` | 1000-600000 ms; każda linia odnawia czas. Po restarcie nadzór jest wyłączony. |
| Zakończyć test | `stop` | Zakończenie testu i zwolnienie jego wyjść; ECU zeruje żądanie testu. |
| Zatrzymać napęd VP37 | `X` | Zatrzymuje pompę; ponowna inicjalizacja wymaga restartu ECU. |
| Zadać pozycję spoczynkową | `S0` | Regulator dalej pracuje i może zapisywać kroki przy zerowym żądaniu. |
| Zapisać pełny przebieg | `B2`, potem `B0` | Początek i koniec strumienia; odbierz końcowy rekord i sprawdź kompletność. |

## Czytanie nastaw

Nazwy wyjaśniają rolę nastawy; symbole konsoli służą do jej zmiany. Definicje i zakresy
wszystkich nastaw należą do [vp37_tuning.c](../../common/vp37/vp37_tuning.c) oraz
[vp37_config.h](../../common/vp37/vp37_config.h).

| Nastawa | Symbol | Domyślnie / zakres | Kiedy wpływa na wynik |
| --- | --- | --- | --- |
| Limit fizycznego skoku | `X13` | 90%; powyżej 0 do 100% | Mapuje pełne żądanie na część skalibrowanego skoku. Nie zmienia fizycznej skali map. |
| Pomoc podczas zejścia | `X25` | Wyłączona; 0 lub 1 | Opcja badawcza wycofująca nadmiar całki przy zejściu. |
| Zachowana korekta zejścia | `X26` | 0,4; od 0,25 do 0,75 | Przy aktywnym `X25`, jeśli brak wyuczonej korekty dla celu. To udział korekty, bez jednostki. |

[Opis sterowania](CONTROL.pl.md) wyjaśnia skalę położenia, człony regulatora i zależności tych opcji.

## Obraz stanowiskowy i sprzęt

Testy istnieją tylko w obrazie BENCH zadeklarowanym w
[hal_project_config.h](../hal_project_config.h) przez `ECU_FUNCTIONAL_TESTS_ENABLED=1`.
Konfiguracja HAL współdzielona z testami hostowymi jest w
[ecu_hal_config.h](../ecu_hal_config.h); deklarację wariantu HAL czyta wyłącznie
z `hal_project_config.h`. W VS Code wybierz `Project: Build variant: BENCH`
lub `Project: Upload variant: BENCH`. Obraz trafia do
`src/ECU/.build/rp2350-arm/rp2350-plus/variants/BENCH/`, a dla montażu
RP2040-Plus pod `.build/rp2040/rp2040-plus/`. Zadania bez wariantu dają obraz
samochodu. Odpowiedniki CLI podaje [instrukcja kompilacji](../../../doc/BUILDING.pl.md).

W `BENCH` opcja `ECU_SC_TESTS_RPM_INTERLOCK` ma domyślnie wartość `0`,
więc testy konfiguratora mogą działać przy RPM podawanym z generatora.
Ustawienie `1` odrzuca lub zatrzymuje testy konfiguratora powyżej
`ECU_SC_TESTS_RPM_LIMIT` rpm. Opcja nie dotyczy testów z konsoli;
szczegóły podają [zasady testów funkcjonalnych](../../common/scDefinitions/PROTOCOL.pl.md#testy-funkcjonalne).

Testy z konsoli mają opcjonalny nadzór. `watch <ms>` uzbraja go z limitem
1000-600000 ms: gdy przez ten czas nie przyjdzie żadna linia konsoli,
działający test z konsoli zatrzymuje się z wynikiem `host_lost`, a zadanie
wraca do zera, jak przy teście konfiguratora po utracie hosta. Każda linia
konsoli odnawia nadzór, samo `watch` odnawia go i podaje limit, `watch 0`
wyłącza, a po restarcie firmware jest wyłączony. Narzędzie stanowiska, które
trzyma port, uzbraja nadzór na początku nagrania i odnawia go w czasie pracy;
zabity collector lub odłączony kabel zostawia wtedy nastawnik na ostatnim
zadaniu tylko do upływu limitu. `G` nadal auto-zeruje pojedynczy postój `S`.

Jednorazowe czynności DTC i KV wykonuje rdzeń 0. Rdzeń sterowania nadal
obsługuje VP37 i przesuwa sekwencję testów po publikacji wyniku.
Zatrzymanie testu anuluje czynność, która jeszcze nie zaczęła się wykonywać.
`run dtc` podaje `ok`, gdy U0073 (`DTC_CAN_BUS_FAULT`) jest aktywny
z detalem transmisji RPM (`DTC_DETAIL_CAN_TX_RPM`), albo `failed`
w pozostałych przypadkach. Wynik sprawdza stan w pamięci zaraz po
wstrzyknięciu błędu; nie potwierdza zapisu trwałego po restarcie.
Odczytaj kod przez OBD, po próbie skasuj DTC i ponów odczyt,
aby potwierdzić skasowanie.
Polecenia odczytu `list` i `params` również drukuje rdzeń konsoli, gdy
callback portu szeregowego zwolni tymczasowe wyciszenie debug.
Komunikaty startu, zatrzymania i wyniku testu trafiają tam przez ograniczoną
kolejkę. Ich wydruk nie blokuje sterowania na mutex debug. Przepełnienie jest
zgłaszane; zatrzymanie nastawnika nadal działa.

[device_tree.json](../device_tree.json) przypisuje piny i adresy. Montaże
`rp2350-plus` (Waveshare RP2350-Plus, domyślny) i `rp2040-plus` (Waveshare
RP2040-Plus) mają wspólne PCB i okablowanie; wybiera je `--assembly` lub
wybór montażu w VS Code. Interfejsy:

- **I²C 400 kHz:** ECU master, Adjustometer `0x57`, PCF8574A `0x38`.
  PCF8574A dopuszcza 100 kHz, więc pracuje poza specyfikacją do wymiany na
  PCA8574A lub wydzielenia magistrali. Typ lokalny w `bindings/` ogranicza
  magistralę do 400 kHz; szybsza konfiguracja nie przejdzie kontroli drzewa.
- **SPI:** dwa MCP2515, CAN0 dla samochodu, CAN1 dla OBD-II.
- **ADC:** sześć wejść przez HC4051 (temperatury płynu, oleju i powietrza,
  przepustnica, paliwo, ciśnienie dolotu), zasilanie oraz bocznik VP37 na
  GPIO26 zarezerwowanym dla tego pomiaru. Skan prądu VP37 zajmuje ADC;
  pozostałe odczyty analogowe korzystają wtedy z jego bufora.
- **PWM:** VP37 i N75; **GPIO:** kontrolka DPF; **przerwanie GPIO:** Hall
  silnika.
- **EEPROM emulowany we flash:** DTC i ustawienia.

## Telemetria

Pola pętli prądowej: `cen` (włączenie), `cuse` (świeży dopasowany pomiar),
`ctar` (ostatni cel w A), `cref` (cel historyczny w A), `cerr` (historyczny
cel minus prąd ON w A), `cpwm` (nominalna korekta) i `cage` (wiek środka ON
w µs). Występują też w IPULSE z `latch`, `lp`, `lduty` i `lv`: odtworzonym
czasem zatrzaśnięcia, okresem, wypełnieniem i poprawnością. `dma`, `take`
i `reduce` to czasy MCU zakończenia DMA, zachowania i redukcji;
`reduce - (us + on)` określa opóźnienie dostarczenia po końcu ON.

Stanowiskowe IWAVE, najwyżej co 100 ms, używają identyfikatora `us` IPULSE
i podają osiem średnich ON w kolejności czasowej z czasami próbek.
Pomijają marginesy przy zboczach i nie mierzą zaniku prądu swobodnego.
`gl` zlicza odrzucone zakłócenia bramki od resetu historii. Obraz stanowiskowy
przyjmuje `C0`/`C1` do porównania sprzężenia wyłączonego i włączonego przez
jedno API położenia. Test modelu elektrycznego nie potwierdza stabilności
mechanicznej; postoje u góry i ruch wymagają walidacji sprzętowej.

## Zapis kalibracji i przebiegu

Podczas kalibracji startowej obraz BENCH używa tej samej pamięci do zapisu
każdej próbki kwalifikującej MIN/MAX. Po połączeniu USB linie `VP37 CAL`
przekazują położenie, częstotliwość surową i filtrowaną, baseline, numer próbki,
oba zegary, świeżość, status, zadane PWM, napięcie w 0,1 V i temperaturę paliwa
w stopniach C. `VP37 CAL END` podaje liczbę próbek, zakwalifikowane końce,
poprawność wyniku i ewentualne przepełnienie bufora. Parametry okna estymatora
i timeoutu definiuje `vp37_config.h`. Zwykły strumień `VP37 C` pokazuje powrót
do spoczynku po kalibracji; kalibracja nie próbkuje prądu. Zwykły zapis śladu
RAM pozostaje zajęty do zakończenia tego eksportu.

Do metryk całego przebiegu służą `B2`, które rozpoczyna strumień, oraz `B0`,
które go kończy. Rdzeń sterowania zapisuje każdy krok w buforze śladu,
a rdzeń konsoli odbiera go na bieżąco jako linie `VP37 N`. `B` i `B1`
zachowują tryby skończonego nagrania; kalibracja i wszystkie tryby nagrywania
korzystają z tej samej pamięci. Konsola nadal drukuje `VP37 C` do nadzoru.
Odbiera `N` w każdym obiegu swojej pętli, niezależnie od interwału raportu C,
z budżetem 20 ms na obieg. Rozpoczęty zapis USB może przekroczyć ten budżet.
Każda linia `N` zawiera CRLF i trafia do jednego zapisu szeregowego bez
prefiksu debugowego `ECU:`, co usuwa osobne transfery USB prefiksu i końca
linii.

Okna metryk wyznaczaj według czasu MCU `us` i kolejnych numerów kroków `n`,
nie według chwili odbioru buforowanej linii na hoście. `VP37 STREAM END`
podaje czasy nagrania, liczbę próbek, numery pierwszego i ostatniego kroku
oraz `overflow`. Odrzucaj niekompletne nagrania, luki w numeracji,
niezgodność liczby próbek i przepełnienie. Zapis nie czeka na USB: gdy
odbiorca nie nadąża, nagranie kończy się z `overflow:1`. Zatrzymanie pompy
również kończy strumień; `S0` zachowuje zapis kroków sterowania w spoczynku.
Odbierz rekord końcowy przed kolejnym nagraniem lub zamknięciem collectora.

Każda próbka `N` zawiera zadane i rampowane położenie, feedback, czasy,
liczbę próbek R (`rn`), aktywność kompensacji R (`ruse`) oraz mnożnik
termiczny (`tsc10000`, mnożnik razy 10000). `thr10` podaje żądanie w dziesiątych
częściach procenta. Konsola składa całą linię N przed wysłaniem jej do USB.
Ostatni skan prądu dostarcza `latch`, `lp` i `lv`;
`cmd_us` ma znaczenie tylko przy `match:1`. Te obserwacje mogą poprzedzać
zapisany krok i same nie identyfikują pierwszego zatrzasku PWM po komendzie.
Przez całe nagranie utrzymuj dotychczasowe guardy oparte na C.
