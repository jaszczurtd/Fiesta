# Przeglądarka pomiarów VP37

[English](README.md)

Viewer pokazuje telemetrię VP37 na żywo oraz tworzy strony z zapisanych pomiarów i porównań.
Strony offline działają przez `file://`, bez serwera i zewnętrznych bibliotek wykresów.

- **Mam ECU podłączone przez USB:** wykonaj poniższe kroki w VS Code.
- **Mam zapisane pomiary:** przejdź do [builda i pracy offline](#build-i-praca-offline).
- **Inny program już zapisuje log:** użyj [serwera rosnącego logu](#serwer-rosnącego-logu).

## Podgląd ECU w VS Code

Najpierw przygotuj sprzęt według [instrukcji ECU](../../src/ECU/README.pl.md). Wgrywany
firmware wykonuje kalibrację nastawnika przy starcie.

1. Otwórz folder **`src/ECU`**. Potrzebujesz CMake i kompilatora C/C++ dla hosta; na Windows MSVC Build Tools.
2. Uruchom **Tasks: Run Task -> `VP37: Live viewer`**. Zadanie buduje viewer oraz przebudowuje i wgrywa **BENCH ECU** dla wybranego assembly.
3. Po komunikacie `Connected to ...` odczytaj URL w terminalu. Domyślnie jest to **<http://127.0.0.1:8037/live>**.
4. Otwórz adres w przeglądarce www albo przez **Open Integrated Browser** w VS Code. Pozostaw zadanie uruchomione.

Zapis trafia do `src/ECU/.build/vp37.log`. Kolejne uruchomienie dopisuje nową sesję i
ponownie wykonuje build/upload. Widok pokazuje nowe próbki; połączenie nie odtwarza
wcześniejszej historii.

**Ctrl+C** lub **Tasks: Terminate Task** kończy przechwytywanie i serwer. Zamknięcie karty
kończy tylko podgląd. Żadna z tych czynności nie jest poleceniem zatrzymania nastawnika;
zakończenie testu opisuje instrukcja ECU.

## Widok na żywo

Strony live i offline domyślnie używają angielskiego. Przycisk
**EN -> PL / PL -> EN** przełącza język i zapamiętuje wybór, gdy przeglądarka
udostępnia pamięć ustawień. Linki również przenoszą wybór języka, także przy
zablokowanej pamięci. Przełączenie widoku live zachowuje odbiór, pauzę, zoom
i wybrany ślad. Strony offline wczytują się ponownie w wybranym języku.

Każdy wykres ma krótki opis i kolorową legendę obok, a w wąskim oknie - nad
wykresem. Najedź na nazwę parametru, aby zobaczyć klucz z logu, jednostkę
i objaśnienie; wybierz ją klawiszem Tab lub kliknij, aby rozwinąć opis.
Napływ próbek pozostawia te opisy otwarte. Surowa częstotliwość czujnika
i korekta PID mają osobne wykresy, aby ich skale nie utrudniały odczytu
położenia i wyjścia PWM. CSV zachowuje identyfikatory pól. PNG używa wybranego
języka i zawija nazwy w legendzie. Zapisane raporty i komunikaty logu zachowują
tekst źródłowy. Napięcie Adjustometera ma jednostkę decywolt (`dV`); jednostki
PWM nie oznaczają procentów.

Panele pokazują położenie i uchyb, PWM i człony PID, pasy stanów regulatora,
prąd, napięcia i temperaturę, czasy pętli oraz wartości i bity statusu
Adjustometera. Jednostki i opisy pochodzą z `field_catalog()`, a legenda
oznacza brakujące pola. Ruch myszy pokazuje wspólny kursor, przeciągnięcie
ustawia wspólny zoom, dwuklik przywraca zachowany zakres. Strony offline
i ich kontrolki nadal działają przez `file://`.

Przesuwające się okno obejmuje 60 sekund. Pauza i zoom zachowują jedną
migawkę zakresu przy dalszym odbiorze; Wznów wraca do najnowszych próbek.
Podczas odbioru wykresy odświeżają się najwyżej dziesięć razy na sekundę;
ukryta karta nie rysuje. CSV zawiera rekordy liczbowe i zdarzenia z oglądanego
zakresu, razem z czasem źródłowym i numerami segmentów. CSV poprzedza apostrofem
komórki tekstowe rozpoczynające się od znacznika formuły arkusza. PNG zapisuje
wyświetlane panele i zakres. Powrót przyciskiem Wstecz ponownie łączy odbiór
i zachowuje widok z pamięci przeglądarki, w tym zatrzymany zakres.

Ponowne połączenie, zmiana pliku, luka w sekwencji i restart zegara MCU
rozdzielają segmenty. Ich granice mają znaczniki; krzywe nie łączą segmentów,
brakujących wartości ani próbek odległych o ponad dwie sekundy.
Czas live zaczyna się od zera, a nowy segment 0,1 sekundy po poprzednim.
Ten separator graficzny nie mierzy czasu rozłączenia. CSV zachowuje
oryginalny czas parsera. Ponowne połączenie nie odzyskuje opuszczonych próbek,
także gdy serwer powtórzy numer epoki.

Próbki `T` są zbierane oddzielnie. `trace end samples:N` otwiera widok śladu,
jeśli użytkownik nie zatrzymał innego zakresu. Zachowane ślady wybiera się
z listy. Oś śladu pokazuje sekundy od pierwszej próbki MCU; CSV zachowuje
także czas parsera. Potwierdzenie `trace: HAL_OK samples:N` zamyka niedokończony
poprzedni zrzut jako niekompletny.
Niezgodna liczba próbek, przerwanie, brak `us`, niemonotoniczny czas lub
przekroczenie pojemności oznaczają niekompletny zrzut. Wybrany ślad pozostaje
dostępny podczas odbierania następnych.

## Build i praca offline

Z katalogu Fiesty, po przygotowaniu przypiętych zależności HAL:

```sh
cmake -S tools/vp37-viewer -B tools/vp37-viewer/.build -DCMAKE_BUILD_TYPE=Release
cmake --build tools/vp37-viewer/.build --parallel
ctest --test-dir tools/vp37-viewer/.build --output-on-failure
```

Program powstaje jako `tools/vp37-viewer/.build/vp37-viewer`; generator
z wieloma konfiguracjami umieszcza go w katalogu wybranej konfiguracji.

Dla generatora Visual Studio podaj przy konfiguracji `-A x64` lub `-A Win32`,
przy budowaniu `--config Release`, a przy uruchamianiu CTest `-C Release`.
Testy wymagają Chrome, Chromium albo Edge. CMake wykrywa przeglądarkę;
można ją też wskazać przez `-DVP37V_BROWSER=/path/to/browser`. Harness w C
uruchamia skrypty produkcyjne w trybie headless, bez Node i npm.
`-DBUILD_TESTING=OFF` buduje samo narzędzie i nie wymaga przeglądarki.

Użyj osobnych katalogów wyjściowych: `build` zastępuje wygenerowane pliki i usuwa nieaktualne
wyniki z katalogów site i cache. Logi wejściowe pozostają tylko do odczytu.

```sh
vp37-viewer parse /path/to/recording.log.gz
vp37-viewer build --root /path/to/recordings --site /path/to/pages \
  --cache /path/to/cache --descriptions /path/to/descriptions
vp37-viewer describe --root /path/to/recordings --cache /path/to/cache \
  --descriptions /path/to/descriptions
```

Wszystkie katalogi danych i wyników trzeba podać jawnie. Pod katalogiem
nagrań znajdują się `vp37_implementation_tests/`, `measurements/` lub
`archive/measurements/`. `describe` wypisuje proponowane opisy;
`--write` je zapisuje, a `--overwrite` pozwala zastąpić istniejące opisy.

Czytnik obsługuje `.log` i pliki `.log.gz` z jednym członem gzip. Archiwa
pomiarów `.tar.gz` trzeba wcześniej rozpakować. Metryki są wiązane po
tożsamości logu i oknie analizy. Po przeniesieniu danych czytnik rozpoznaje
katalog obszaru pomiarów, bez zależności od nazwy workspace.

## Serwer rosnącego logu

```sh
vp37-viewer serve --log /path/to/collector.log --site /path/to/pages
```

Serwer czyta log od początku, śledzi dopisywane bajty i udostępnia wcześniej
wygenerowane strony offline przez HTTP. Oba listenery działają wyłącznie na
`127.0.0.1`: HTTP na porcie 8037, WebSocket na 8038. Porty zmieniają opcje
`--port` i `--ws-port`. Ctrl+C zatrzymuje program. Log pozostaje tylko do odczytu;
serwer nie otwiera portu szeregowego ani nie tworzy brakującego logu.

Oscyloskop otwiera się pod `http://127.0.0.1:8037/live`. Opcjonalne `--site`
udostępnia strony offline pod `/`; bez niego `/` otwiera oscyloskop.

## Konfiguracja i ograniczenia

[Opis techniczny](REFERENCE.pl.md) zawiera zmianę ścieżki logu i portów, skróty klawiaturowe,
zasady przechwytywania, limity buforów, protokół WebSocket i parser.
