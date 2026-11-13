# ECU: przygotowanie i pierwsze uruchomienie

[English](README.md) · [Stan projektu](../../doc/STATUS.pl.md)

ECU steruje nastawnikiem pompy VP37, doładowaniem, świecami żarowymi,
wentylatorem, grzałką bloku i ogrzewaniem przedniej szyby. Zapisuje też DTC
i odpowiada na OBD-II jako Ford Fiesta 1.8 DI EEC-V. Projekt jest prototypem;
prace nad silnikiem i VP37 odbywają się na osobnym stanowisku silnikowym.

Ten przewodnik prowadzi przez uruchomienie istniejącego stanowiska i podgląd
pomiarów. Algorytm opisuje strona [jak działa regulacja VP37](guides/CONTROL.pl.md).

## Przed włączeniem stanowiska

- Ustal wersję płytki i połączenia. Obecne warianty `rp2350-plus` i
  `rp2040-plus` używają tego samego okablowania ECU. Nowsza PCB 3 wymaga
  dostosowania magistral I²C i polaryzacji wyjść w firmware; zobacz
  [zgodność sprzętu z firmware](../../doc/STATUS.pl.md#sprzęt-i-firmware).
- Przygotuj repozytorium i toolchain według [instrukcji kompilacji](../../doc/BUILDING.pl.md).
  Otwórz w VS Code folder **`src/ECU`** i wybierz assembly odpowiadające
  zamontowanej płytce procesora.
- Sprawdź [zasilanie i uruchomienie Adjustometera](../Adjustometer/README.pl.md).
  Pomiar bazowy musi powstać przy wyłączonym napędzie i nieruchomym mechanizmie.
  Po jego zakończeniu zrestartuj ECU. Kalibracja przy starcie ECU może
  przemieścić nastawnik przez jego zakres.

## Otwórz pomiary na żywo

1. W VS Code wybierz **Terminal -> Run Task -> `VP37: Live viewer`**.
   Zadanie buduje viewer, przebudowuje wariant **BENCH** ECU i wgrywa go do
   wybranego assembly. Każde uruchomienie powtarza build i upload.
2. Zaczekaj na połączenie szeregowe i adres strony w terminalu.
3. Otwórz **<http://127.0.0.1:8037/live>** w przeglądarce www albo uruchom
   **`Open Integrated Browser`** w VS Code i wpisz ten adres.
4. Pozostaw zadanie uruchomione. Sprawdź połączenie strony i napływ nowych
   próbek sterowania. Widok startuje po angielsku; przycisk **EN / PL**
   przełącza język.

Ta procedura używa wariantu BENCH projektu ECU. Osobny projekt
[VP37TestBench](../VP37TestBench/README.pl.md) ma własny sprzęt i nie jest
obrazem używanym przez to zadanie.

Domyślny build ECU przekazuje żądanie kierowcy do regulatora położenia.
Nadzorca rozruchu i biegu jałowego jest opcjonalny i domyślnie wyłączony
(`ECU_ENGINE_CONTROL_ENABLED=0`). Wykres położenia nie potwierdza więc
włączenia ani walidacji całej strategii sterowania silnikiem.

## Zakończ sesję

Zamknięcie karty przeglądarki zamyka tylko widok. **Ctrl+C** lub
**Terminate Task** zatrzymuje serwer i zapis portu, ale nie zatrzymuje nastawnika.
Zakończ aktywny test przez konsolę albo configurator, który nim steruje,
zanim zamkniesz sesję. Konsolowe `stop` kończy test i zeruje jego żądanie;
`X` zatrzymuje sam VP37, a ponowna inicjalizacja wymaga restartu ECU.
Po zakończeniu testu może wrócić zwykłe żądanie kierowcy. Użyj `X`,
jeśli sam VP37 ma pozostać zatrzymany. Zobacz [zasady sesji i zapisu](guides/BENCH.pl.md).
Nie otwieraj drugiego programu na porcie zajętym przez zadanie viewera.

## Gdy nie widać pomiarów

| Objaw | Co sprawdzić |
| --- | --- |
| Zadanie kończy się przed połączeniem | Wynik builda, zainstalowany kompilator hostowy i wybrane assembly firmware. |
| Brak połączenia szeregowego | Właściwe urządzenie USB ECU oraz monitor lub configurator zajmujący port. |
| Strona nie może się połączyć | Czy zadanie nadal działa i czy adres zgadza się z URL wypisanym przez zadanie. |
| Jest połączenie, brak nowych próbek sterowania | Czy terminal pokazuje nowe rekordy `VP37 C` i czy wgrany obraz to BENCH. Samo połączenie portu nie potwierdza napływu telemetrii. |
| Niepoprawna pozycja lub kalibracja | Zasilanie, pomiar bazowy i sprzężenie Adjustometera; wykonaj jego procedurę startu przed restartem ECU. |

Zapisane logi, obsługę CLI, wykresów i błędów przechwytywania opisuje
[instrukcja viewera](../../tools/vp37-viewer/README.pl.md).

<a id="wykonanie-i-synchronizacja"></a>
<a id="obroty-silnika"></a>
<a id="zadawanie-położenia-i-przygotowanie-wejścia"></a>
<a id="regulacja-położenia"></a>
<a id="kompensacja-napięcia-i-rezystancji"></a>
<a id="sprzężenie-prądowe"></a>
<a id="telemetria"></a>
<a id="badawczy-profil-regulacji"></a>
<a id="mapa-źródeł"></a>
<a id="obraz-stanowiskowy-i-sprzęt"></a>
<a id="budżet-ram-sterowania"></a>

## Dalsza praca

- [Zrozumienie żądania, położenia i kompensacji](guides/CONTROL.pl.md).
- [Polecenia stanowiska i zapis pełnego przebiegu](guides/BENCH.pl.md).
- [Zmiana harmonogramu, buforów lub kodu](guides/IMPLEMENTATION.pl.md).
- [Kompilacja, wgrywanie i debugowanie bez viewera](../../doc/BUILDING.pl.md).
- [Testy hostowe](../../doc/TESTING.pl.md) i [zakres MISRA](../../MISRA.pl.md).
