# Viewer VP37: konfiguracja i opis techniczny

[English](REFERENCE.md) · [Instrukcja viewera](README.pl.md)

## VS Code w projekcie ECU

Otwórz `src/ECU` jako folder VS Code. Przy dostępnym CMake i natywnym
kompilatorze C/C++ (MSVC Build Tools na Windows) uruchom **Tasks: Run Task ->
VP37: Live viewer**. Zależności taska konfigurują i budują Release w
`.build/vp37-viewer`, następnie przebudowują i wgrywają wariant ECU **BENCH**,
korzystając z wybranego assembly i zweryfikowanej tożsamości urządzenia.
Po uploadzie viewer uruchamia trwały monitor szeregowy i zapisuje jego wyjście. Task działa na
Linuksie i Windows, także ze spacjami w ścieżce projektu. Korzysta z
przypiętych zależności HAL, a sam build nie wymaga przeglądarki.

Task dopisuje telemetrię ze znacznikami czasu do `src/ECU/.build/vp37.log`
i śledzi ten plik. Osobny collector nie jest potrzebny. Aby zmienić miejsce
zapisu, zmień argument po `--log` w
[`src/ECU/.vscode/tasks.json`](../../src/ECU/.vscode/tasks.json), wskazując
inny log. Zachowaj ścieżkę jako jeden ciąg JSON, bez dodatkowych
cudzysłowów powłoki; na Windows używaj `/` albo separatorów `\\`.

Po `Connected to ...` i separatorze terminal pokazuje adres podglądu oraz
instrukcję dla Chrome i VS Code. Otwórz `http://127.0.0.1:8037/live` w przeglądarce www,
albo wybierz **Open Integrated Browser** z palety poleceń VS Code i wklej ten
adres. Komunikat uwzględnia wybrany port HTTP i pojawia się także po ponownym
połączeniu. Pozostaw zadanie uruchomione podczas oglądania danych.
Przenieś kartę przeglądarki do osobnej grupy edytorów,
żeby zachować widoczny kod. Nowe próbki pojawiają się automatycznie;
nowe połączenie nie otrzymuje wcześniejszej historii.

Zatrzymaj serwer przez Ctrl+C w jego terminalu albo **Tasks: Terminate
Task**. Zamknięcie karty przeglądarki pozostawia serwer uruchomiony.
Przed startem drugiego viewera na portach 8037/8038 zatrzymaj pierwszy;
konflikt portów kończy start błędem. Inne porty ustaw przez `--port`
i `--ws-port` w tasku oraz adres HTTP w skrócie. Błąd konfiguracji lub
builda zatrzymuje łańcuch zależności; popraw zgłoszony błąd i ponów task.

## Skróty klawiaturowe

Aby korzystać z klawiatury, wybierz **Preferences: Open Keyboard Shortcuts
(JSON)** i dodaj do istniejącej tablicy użytkownika:

```json
{
  "key": "ctrl+shift+f11",
  "command": "workbench.action.tasks.runTask",
  "args": "VP37: Live viewer"
},
{
  "key": "ctrl+shift+f12",
  "command": "simpleBrowser.api.open",
  "args": "http://127.0.0.1:8037/live"
}
```

Ctrl+Shift+F11 uruchamia task; Ctrl+Shift+F12 otwiera widok, gdy serwer
jest gotowy. To skróty użytkownika: `.vscode/keybindings.reference.json`
jest generowanym przykładem, nie aktywnym plikiem skrótów. Synchronizacja
projektu zachowuje taski viewera i ich argumenty logu, a plik referencyjny
generuje ponownie.

## Zapis portu szeregowego

Monitor zachowuje blokadę portu i ponowne łączenie, także zwalnianie portu
na czas kolejnego uploadu. Zatrzymanie taska kończy zapis i serwer.
Ponowne uruchomienie dopisuje do logu i powtarza build/upload BENCH.
Przechwytywanie zaczyna odczyt na dotychczasowym końcu logu, przed dopisaniem
znacznika sesji. Poprzednie sesje nie opóźniają więc bieżących próbek.
Samo `serve --log PATH` nadal śledzi plik z zewnętrznego collectora.
Opcja `--monitor-project /path/to/ECU` uruchamia zapis przez monitor wskazany
w `.vscode/settings.json` projektu. Zapis usuwa kody ANSI konsoli, dodaje czas
hosta, zachowuje prefiks `ECU:` i opróżnia bufor po każdej kompletnej linii.
Niepełna linia na końcu procesu jest odrzucana. Do danego logu może zapisywać
tylko jeden viewer; sąsiedni plik `.capture.lock` może pozostać po zakończeniu.
Bajt NUL lub linia konsoli dłuższa niż 65408 bajtów kończy przechwytywanie
i serwer błędem. Limit pozostawia miejsce na czas i prefiks; wcześniejsze
kompletne linie pozostają w logu.

## Limity widoku

| Zachowane dane | Limit |
| --- | --- |
| Wiersze liczbowe | Po 4096 dla `C` i `IPULSE`; po 512 dla `CFG` i `ADJ` |
| Opisy pól | 256 wpisów katalogu; nieznane klucze są liczone i odrzucane |
| Zdarzenia | 256, każde do 256 znaków |
| Ślady | Cztery zakończone zrzuty i jeden odbierany; do 2048 wierszy na zrzut |
| Pauza | Jedna dodatkowa migawka, najwyżej limity wierszy live albo jeden ślad |

Przy szybkim strumieniu limity wierszy mogą skrócić zachowane okno. `IWAVE`
jest przesyłane, lecz nie ma panelu ani bufora w przeglądarce. Viewer offline
zachowuje dotychczasowe reguły eksportu.

## Serwer HTTP i protokół WebSocket

`/api/fields` udostępnia wspólny katalog pól, `/api/status` stan w JSON,
a `/live-assets/` pliki interfejsu live dostarczone z programem. Przy `--site`
strony offline korzystają z własnych plików `/assets/`, także gdy pochodzą
ze starszej wersji viewera. Pliki statyczne muszą być mniejsze niż 512 KiB; pliki osiągające
ten limit lub większe otrzymują HTTP 413. Ścieżki poza wskazanymi katalogami są odrzucane. HTTP i WebSocket
obsługują po czterech klientów.

Żądania HTTP i handshake WebSocket mają po 4096 bajtów bufora na linię
żądania i nagłówki. Największe przyjmowane żądanie ma 4094 bajty; przy
4095 bajtach serwer odrzuca je kodem HTTP 413. Dłuższe żądanie może zamiast
tego zakończyć się resetem TCP, jeśli przy zamknięciu socketu pozostały
nieodczytane bajty.

Endpoint WebSocket to `ws://127.0.0.1:8038/ws`. Wersja 1 protokołu wysyła:

| Wiadomość | Znaczenie |
| --- | --- |
| `hello` | `protocol`, bieżąca `epoch`, numer następnej paczki `next`, obecność logu `present` i `ws_port`. |
| `reset` | Te same pola po pojawieniu się, zniknięciu, obcięciu lub podmianie logu. Klient rozpoczyna nowy segment widoku. |
| `samples` | `epoch`, rosnący `seq` i `records`. Rekord zawiera `kind`, czas `t` w sekundach oraz liczbowe `values` albo tekst zdarzenia `text`. Brakujące i nieskończone wartości to JSON `null`. Rekord sterowania z `restart: true` rozpoczyna nowy segment widoku. |

Paczki wychodzą zwykle co 40 ms, a po osiągnięciu 32 KiB wcześniej. Rekordy
korzystają z czasu MCU, gdy jest dostępny, według zasad poniżej. `HOST`, `TEST`
i pozostałe komunikaty VP37, w tym koniec śladu, zachowują tekst. Nie oznacza
to dodania interpretacji rekordów kalibracji.

Nowe połączenie otrzymuje kolejne paczki, bez odtwarzania wcześniejszej historii.
Numery paczek pozwalają wykryć luki. Po ponownym połączeniu klient zaczyna od
`hello`; restart serwera rozpoczyna nowy strumień. Klient, który nie przyjmuje
całej ramki, zostaje rozłączony. Serwer nie kolejkuje danych ani nie ponawia
częściowo wysłanej ramki. Odrzuca wiadomości danych od klientów; endpoint służy
wyłącznie do publikowania pomiarów.

Czytnik zachowuje niepełną linię na końcu pliku i odrzuca ją po wykryciu zmiany
logu. Rozpoznaje ją po tożsamości pliku, rozmiarze i ostatnich 64 odczytanych
bajtach. Nadpisania, które między odczytami odtworzy te same bajty i rozmiar,
nie da się odróżnić od zwykłego dopisywania. Przy rotacji używaj atomowej podmiany.
Jedno wywołanie `poll` czyta najwyżej 64 KiB. Limit linii wynosi 64 KiB,
rekordu liczbowego 256 różnych pól, a rekordu po zakodowaniu 32512 bajtów.
Przekroczenie limitu lub błąd odczytu kończy program z błędem. Na brakujący plik
serwer czeka. Historia parsera i kolejki klientów nie rosną z długością logu.

## Parser

`src/telemetry.h` udostępnia `TelemetryParser::feed()` i `finish()`. Fragment
wejścia może skończyć się w dowolnym miejscu linii; przed `finish()` parser
oddaje tylko kompletne linie. Limit linii to 64 KiB. Błąd zatrzymuje dany
czytnik; po obcięciu lub podmianie pliku odrzuć częściowy wynik i utwórz nowy.

Domyślna oś zachowuje czas collectora, zgodnie z oknami zapisanych metryk.
Opcjonalny tryb MCU obsługuje przepełnienie `us` i umieszcza opóźnione próbki
`T`, `IPULSE` i `IWAVE` względem rekordów sterowania. Cofnięcie czasu
sterowania rozpoczyna nową epokę. Tak samo działa odchyłka wyliczonego czasu
sterowania od czasu hosta większa niż pięć sekund; rekord otrzymuje wtedy
znacznik `restart`. Tolerancja dopuszcza buforowanie transmisji i pozwala
wykrywać restarty przypominające przepełnienie. Kolejne obserwacje sterowania
muszą dzielić odstępy mniejsze niż 2^31 mikrosekund. Restart mieszczący się
w tolerancji czasu hosta względem rzeczywistego przepełnienia pozostaje
niejednoznaczny bez jawnego znacznika. Linie bez `us` zachowują czas collectora.
Pola numeryczne każdego `CFG` są przechowywane osobno od podsumowań rewizji.

`parse_log()` korzysta z tego samego czytnika. Dzielenie na linie i obliczenia
czasu są w C11; istniejący model nagrania i generator stron pozostają w C++17.
Zmiana wersji parsera unieważnia starszy cache po zmianie jego wyników.

Konstruktor z callbackiem przekazuje pojedyncze rekordy bez zachowywania
nagrania. Przekazane wartości są ważne tylko podczas callbacka; zwrócony błąd
zatrzymuje czytnik. Parsowanie offline zachowuje dotychczasowe działanie.
Interpretacja rekordów kalibracji (`CAL`) czeka na implementację. Viewer offline zgłasza nieznane pola
i rodzaje linii.

Testy przeglądarki uruchamia również hostowy build ECU, w lokalnej bramce
i CI. Czytają wyłącznie niezależne dane z `tests/fixtures/`.
